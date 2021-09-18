/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "CrossOriginOpenerPolicy.h"

#include "CrossOriginEmbedderPolicy.h"
#include "HTTPHeaderNames.h"
#include "HTTPParsers.h"
#include "PingLoader.h"
#include "ReportingEndpointsCache.h"
#include "ResourceResponse.h"
#include "ScriptExecutionContext.h"
#include <wtf/JSONValues.h>

namespace WebCore {

// https://html.spec.whatwg.org/multipage/origin.html#sanitize-url-report
static String sanitizeReferrerForURLReport(const URL& referrer)
{
    URL sanitizedReferrer = referrer;
    sanitizedReferrer.removeCredentials();
    sanitizedReferrer.removeFragmentIdentifier();
    return sanitizedReferrer.string();
}

static ASCIILiteral crossOriginOpenerPolicyToString(const CrossOriginOpenerPolicyValue& coop)
{
    switch (coop) {
    case CrossOriginOpenerPolicyValue::SameOrigin:
    case CrossOriginOpenerPolicyValue::SameOriginPlusCOEP:
        return "same-origin"_s;
    case CrossOriginOpenerPolicyValue::SameOriginAllowPopups:
        return "same-origin-allow-popups"_s;
    case CrossOriginOpenerPolicyValue::UnsafeNone:
        break;
    }
    return "unsafe-none"_s;
}

static ASCIILiteral crossOriginOpenerPolicyValueToEffectivePolicyString(CrossOriginOpenerPolicyValue coop)
{
    switch (coop) {
    case CrossOriginOpenerPolicyValue::SameOriginAllowPopups:
        return "same-origin-allow-popups"_s;
    case CrossOriginOpenerPolicyValue::SameOrigin:
        return "same-origin"_s;
    case CrossOriginOpenerPolicyValue::SameOriginPlusCOEP:
        return "same-origin-plus-coep"_s;
    case CrossOriginOpenerPolicyValue::UnsafeNone:
        break;
    }
    return "unsafe-none"_s;
}

// https://html.spec.whatwg.org/multipage/origin.html#obtain-coop
CrossOriginOpenerPolicy obtainCrossOriginOpenerPolicy(const ResourceResponse& response, const ScriptExecutionContext& context)
{
    std::optional<CrossOriginEmbedderPolicy> coep;
    auto ensureCOEP = [&coep, &response, &context]() -> CrossOriginEmbedderPolicy& {
        if (!coep)
            coep = obtainCrossOriginEmbedderPolicy(response, &context);
        return *coep;
    };
    auto parseCOOP = [&response, &ensureCOEP](HTTPHeaderName headerName, auto& value, auto& reportingEndpoint) {
        auto coopParsingResult = parseStructuredFieldValue(response.httpHeaderField(headerName));
        if (!coopParsingResult)
            return;

        if (coopParsingResult->first == "same-origin") {
            auto& coep = ensureCOEP();
            if (coep.value == CrossOriginEmbedderPolicyValue::RequireCORP || (headerName == HTTPHeaderName::CrossOriginOpenerPolicyReportOnly && coep.reportOnlyValue == CrossOriginEmbedderPolicyValue::RequireCORP))
                value = CrossOriginOpenerPolicyValue::SameOriginPlusCOEP;
            else
                value = CrossOriginOpenerPolicyValue::SameOrigin;
        } else if (coopParsingResult->first == "same-origin-allow-popups")
            value = CrossOriginOpenerPolicyValue::SameOriginAllowPopups;

        reportingEndpoint = coopParsingResult->second.get("report-to"_s);
    };

    CrossOriginOpenerPolicy policy;
    if (!context.settingsValues().crossOriginOpenerPolicyEnabled || !SecurityOrigin::create(response.url())->isPotentiallyTrustworthy())
        return policy;

    parseCOOP(HTTPHeaderName::CrossOriginOpenerPolicy, policy.value, policy.reportingEndpoint);
    parseCOOP(HTTPHeaderName::CrossOriginOpenerPolicyReportOnly, policy.reportOnlyValue, policy.reportOnlyReportingEndpoint);
    return policy;
}

CrossOriginOpenerPolicy CrossOriginOpenerPolicy::isolatedCopy() const
{
    return {
        value,
        reportingEndpoint.isolatedCopy(),
        reportOnlyValue,
        reportOnlyReportingEndpoint.isolatedCopy()
    };
}

void addCrossOriginOpenerPolicyHeaders(ResourceResponse& response, const CrossOriginOpenerPolicy& coop)
{
    if (coop.value != CrossOriginOpenerPolicyValue::UnsafeNone) {
        if (coop.reportingEndpoint.isEmpty())
            response.setHTTPHeaderField(HTTPHeaderName::CrossOriginOpenerPolicy, crossOriginOpenerPolicyToString(coop.value));
        else
            response.setHTTPHeaderField(HTTPHeaderName::CrossOriginOpenerPolicy, makeString(crossOriginOpenerPolicyToString(coop.value), "; report-to=\"", coop.reportingEndpoint, '\"'));
    }
    if (coop.reportOnlyValue != CrossOriginOpenerPolicyValue::UnsafeNone) {
        if (coop.reportOnlyReportingEndpoint.isEmpty())
            response.setHTTPHeaderField(HTTPHeaderName::CrossOriginOpenerPolicyReportOnly, crossOriginOpenerPolicyToString(coop.reportOnlyValue));
        else
            response.setHTTPHeaderField(HTTPHeaderName::CrossOriginOpenerPolicyReportOnly, makeString(crossOriginOpenerPolicyToString(coop.reportOnlyValue), "; report-to=\"", coop.reportOnlyReportingEndpoint, '\"'));
    }
}

// https://www.w3.org/TR/reporting/#try-delivery
static void sendCOOPViolationReport(Frame& frame, CrossOriginOpenerPolicy coop, COOPDisposition disposition, const URL& coopURL, const SecurityOrigin& coopOrigin, const String& userAgent, Function<void(JSON::Object&)>&& populateBody)
{
    auto& reportingEndpoint = disposition == COOPDisposition::Reporting ? coop.reportOnlyReportingEndpoint : coop.reportingEndpoint;
    if (reportingEndpoint.isEmpty())
        return;

    auto reportingEndpointsCache = frame.page() ? frame.page()->reportingEndpointsCache() : nullptr;
    if (!reportingEndpointsCache)
        return;
    auto endpointURL = reportingEndpointsCache->endpointURL(coopOrigin.data(), reportingEndpoint);
    if (!endpointURL.isValid())
        return;

    auto body = JSON::Object::create();
    body->setString("disposition"_s, disposition == COOPDisposition::Reporting ? "reporting"_s : "enforce"_s);
    body->setString("effectivePolicy"_s, crossOriginOpenerPolicyValueToEffectivePolicyString(disposition == COOPDisposition::Reporting ? coop.reportOnlyValue : coop.value));
    populateBody(body);

    auto reportObject = JSON::Object::create();
    reportObject->setString("type"_s, "coop"_s);
    reportObject->setString("url"_s, coopURL.string());
    reportObject->setString("user_agent", userAgent);
    reportObject->setInteger("age", 0); // We currently do not delay sending the reports.
    reportObject->setObject("body"_s, WTFMove(body));

    auto reportList = JSON::Array::create();
    reportList->pushObject(reportObject);

    auto report = FormData::create(reportList->toJSONString().utf8());
    PingLoader::sendViolationReport(frame, endpointURL, WTFMove(report), ViolationReportType::StandardReportingAPIViolation);
}

// https://html.spec.whatwg.org/multipage/origin.html#coop-violation-navigation-to
void sendViolationReportWhenNavigatingToCOOPResponse(Frame& frame, CrossOriginOpenerPolicy coop, COOPDisposition disposition, const URL& coopURL, const URL& previousResponseURL, const SecurityOrigin& coopOrigin, const SecurityOrigin& previousResponseOrigin, const String& referrer, const String& userAgent)
{
    sendCOOPViolationReport(frame, coop, disposition, coopURL, coopOrigin, userAgent, [&](auto& body) {
        body.setString("previousResponseURL"_s, coopOrigin.isSameOriginAs(previousResponseOrigin) ? sanitizeReferrerForURLReport(previousResponseURL) : String());
        body.setString("type"_s, "navigation-to-response"_s);
        body.setString("referrer"_s, referrer);
    });
}

// https://html.spec.whatwg.org/multipage/origin.html#coop-violation-navigation-from
void sendViolationReportWhenNavigatingAwayFromCOOPResponse(Frame& frame, CrossOriginOpenerPolicy coop, COOPDisposition disposition, const URL& coopURL, const URL& nextResponseURL, const SecurityOrigin& coopOrigin, const SecurityOrigin& nextResponseOrigin, bool isCOOPResponseNavigationSource, const String& userAgent)
{
    sendCOOPViolationReport(frame, coop, disposition, coopURL, coopOrigin, userAgent, [&](auto& body) {
        body.setString("nextResponseURL"_s, coopOrigin.isSameOriginAs(nextResponseOrigin) || isCOOPResponseNavigationSource ? sanitizeReferrerForURLReport(nextResponseURL) : String());
        body.setString("type"_s, "navigation-from-response"_s);
    });
}

} // namespace WebCore
