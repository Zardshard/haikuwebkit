/*
 * Copyright (C) 2014-2023 Apple Inc. All rights reserved.
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

#if ENABLE(UI_SIDE_COMPOSITING)

#include "RemoteScrollingCoordinatorTransaction.h"

#include "ArgumentCoders.h"
#include "WebCoreArgumentCoders.h"
#include <WebCore/GraphicsLayer.h>
#include <WebCore/ScrollTypes.h>
#include <WebCore/ScrollingStateFixedNode.h>
#include <WebCore/ScrollingStateFrameHostingNode.h>
#include <WebCore/ScrollingStateFrameScrollingNode.h>
#include <WebCore/ScrollingStateOverflowScrollProxyNode.h>
#include <WebCore/ScrollingStateOverflowScrollingNode.h>
#include <WebCore/ScrollingStatePositionedNode.h>
#include <WebCore/ScrollingStateStickyNode.h>
#include <WebCore/ScrollingStateTree.h>
#include <wtf/text/CString.h>
#include <wtf/text/TextStream.h>

namespace IPC {
using namespace WebCore;

template<> struct ArgumentCoder<ScrollingStateNode> {
    static void encode(Encoder&, const ScrollingStateNode&);
    static std::optional<Ref<ScrollingStateNode>> decode(Decoder&);
};

template<> struct ArgumentCoder<ScrollingStateScrollingNode> {
    static void encode(Encoder&, const ScrollingStateScrollingNode&);
    static std::optional<Ref<ScrollingStateScrollingNode>> decode(Decoder&);
};

template<> struct ArgumentCoder<ScrollingStateFrameHostingNode> {
    static void encode(Encoder&, const ScrollingStateFrameHostingNode&);
    static std::optional<Ref<ScrollingStateFrameHostingNode>> decode(Decoder&);
};

template<> struct ArgumentCoder<ScrollingStateFrameScrollingNode> {
    static void encode(Encoder&, const ScrollingStateFrameScrollingNode&);
    static std::optional<Ref<ScrollingStateFrameScrollingNode>> decode(Decoder&);
};

template<> struct ArgumentCoder<ScrollingStateOverflowScrollingNode> {
    static void encode(Encoder&, const ScrollingStateOverflowScrollingNode&);
    static std::optional<Ref<ScrollingStateOverflowScrollingNode>> decode(Decoder&);
};

template<> struct ArgumentCoder<ScrollingStateOverflowScrollProxyNode> {
    static void encode(Encoder&, const ScrollingStateOverflowScrollProxyNode&);
    static std::optional<Ref<ScrollingStateOverflowScrollProxyNode>> decode(Decoder&);
};

template<> struct ArgumentCoder<ScrollingStateFixedNode> {
    static void encode(Encoder&, const ScrollingStateFixedNode&);
    static std::optional<Ref<ScrollingStateFixedNode>> decode(Decoder&);
};

template<> struct ArgumentCoder<ScrollingStateStickyNode> {
    static void encode(Encoder&, const ScrollingStateStickyNode&);
    static std::optional<Ref<ScrollingStateStickyNode>> decode(Decoder&);
};

template<> struct ArgumentCoder<ScrollingStatePositionedNode> {
    static void encode(Encoder&, const ScrollingStatePositionedNode&);
    static std::optional<Ref<ScrollingStatePositionedNode>> decode(Decoder&);
};

} // namespace IPC

using namespace IPC;

void ArgumentCoder<ScrollingStateNode>::encode(Encoder& encoder, const ScrollingStateNode& node)
{
    encoder << node.nodeType();

    switch (node.nodeType()) {
    case ScrollingNodeType::MainFrame:
    case ScrollingNodeType::Subframe:
        encoder << downcast<ScrollingStateFrameScrollingNode>(node);
        break;
    case ScrollingNodeType::FrameHosting:
        encoder << downcast<ScrollingStateFrameHostingNode>(node);
        break;
    case ScrollingNodeType::Overflow:
        encoder << downcast<ScrollingStateOverflowScrollingNode>(node);
        break;
    case ScrollingNodeType::OverflowProxy:
        encoder << downcast<ScrollingStateOverflowScrollProxyNode>(node);
        break;
    case ScrollingNodeType::Fixed:
        encoder << downcast<ScrollingStateFixedNode>(node);
        break;
    case ScrollingNodeType::Sticky:
        encoder << downcast<ScrollingStateStickyNode>(node);
        break;
    case ScrollingNodeType::Positioned:
        encoder << downcast<ScrollingStatePositionedNode>(node);
        break;
    }
}

std::optional<Ref<ScrollingStateNode>> ArgumentCoder<ScrollingStateNode>::decode(Decoder& decoder)
{
    auto nodeType = decoder.decode<ScrollingNodeType>();
    if (!nodeType)
        return std::nullopt;

    switch (*nodeType) {
    case ScrollingNodeType::MainFrame:
    case ScrollingNodeType::Subframe:
        return ArgumentCoder<ScrollingStateFrameScrollingNode>::decode(decoder);
    case ScrollingNodeType::FrameHosting:
        return ArgumentCoder<ScrollingStateFrameHostingNode>::decode(decoder);
    case ScrollingNodeType::Overflow:
        return ArgumentCoder<ScrollingStateOverflowScrollingNode>::decode(decoder);
    case ScrollingNodeType::OverflowProxy:
        return ArgumentCoder<ScrollingStateOverflowScrollProxyNode>::decode(decoder);
    case ScrollingNodeType::Fixed:
        return ArgumentCoder<ScrollingStateFixedNode>::decode(decoder);
    case ScrollingNodeType::Sticky:
        return ArgumentCoder<ScrollingStateStickyNode>::decode(decoder);
    case ScrollingNodeType::Positioned:
        return ArgumentCoder<ScrollingStatePositionedNode>::decode(decoder);
    }
    ASSERT_NOT_REACHED();
    return std::nullopt;
}

static void encodeNodeShared(Encoder& encoder, const ScrollingStateNode& node)
{
    encoder << node.changedProperties();

    if (node.hasChangedProperty(ScrollingStateNode::Property::Layer))
        encoder << node.layer().layerIDForEncoding();

    encoder << node.children();
}

static bool decodeNodeShared(Decoder& decoder, ScrollingStateNode& node)
{
    OptionSet<ScrollingStateNode::Property> changedProperties;
    if (!decoder.decode(changedProperties))
        return false;

    node.setChangedProperties(changedProperties);
    if (node.hasChangedProperty(ScrollingStateNode::Property::Layer)) {
        std::optional<PlatformLayerIdentifier> layerID;
        if (!decoder.decode(layerID))
            return false;
        node.setLayer(layerID.value_or(PlatformLayerIdentifier { }));
    }

    std::optional<Vector<Ref<ScrollingStateNode>>> children;
    decoder >> children;
    if (!children)
        return false;
    node.setChildren(WTFMove(*children));
    return true;
}

#define SCROLLING_NODE_ENCODE(property, getter) \
    if (node.hasChangedProperty(property)) \
        encoder << node.getter();

#define SCROLLING_NODE_ENCODE_ENUM(property, getter) \
    if (node.hasChangedProperty(property)) \
        encoder << node.getter();

#define SCROLLING_NODE_DECODE(property, type, setter) \
    if (node->hasChangedProperty(property)) { \
        type decodedValue; \
        if (!decoder.decode(decodedValue)) \
            return std::nullopt; \
        node->setter(WTFMove(decodedValue)); \
    }

#define SCROLLING_NODE_DECODE_ENUM(property, type, setter) \
    if (node->hasChangedProperty(property)) { \
        type decodedValue; \
        if (!decoder.decode(decodedValue)) \
            return std::nullopt; \
        node->setter(decodedValue); \
    }

#define SCROLLING_NODE_DECODE_REFERENCE(property, type, setter) \
    if (node.hasChangedProperty(property)) { \
        type decodedValue; \
        if (!decoder.decode(decodedValue)) \
            return false; \
        node.setter(WTFMove(decodedValue)); \
    }

static void encodeScrollingStateScrollingNodeShared(Encoder& encoder, const ScrollingStateScrollingNode& node)
{
    encodeNodeShared(encoder, node);

    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::ScrollableAreaSize, scrollableAreaSize)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::TotalContentsSize, totalContentsSize)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::ReachableContentsSize, reachableContentsSize)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::ScrollPosition, scrollPosition)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::ScrollOrigin, scrollOrigin)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::SnapOffsetsInfo, snapOffsetsInfo)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::CurrentHorizontalSnapOffsetIndex, currentHorizontalSnapPointIndex)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::CurrentVerticalSnapOffsetIndex, currentVerticalSnapPointIndex)
#if ENABLE(SCROLLING_THREAD)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::ReasonsForSynchronousScrolling, synchronousScrollingReasons)
#endif
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::IsMonitoringWheelEvents, isMonitoringWheelEvents)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::ScrollableAreaParams, scrollableAreaParameters)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::RequestedScrollPosition, requestedScrollData)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::KeyboardScrollData, keyboardScrollData)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::ContentAreaHoverState, mouseIsOverContentArea)

    if (node.hasChangedProperty(ScrollingStateNode::Property::ScrollContainerLayer))
        encoder << node.scrollContainerLayer().layerIDForEncoding();

    if (node.hasChangedProperty(ScrollingStateNode::Property::ScrolledContentsLayer))
        encoder << node.scrolledContentsLayer().layerIDForEncoding();

    if (node.hasChangedProperty(ScrollingStateNode::Property::HorizontalScrollbarLayer))
        encoder << node.horizontalScrollbarLayer().layerIDForEncoding();

    if (node.hasChangedProperty(ScrollingStateNode::Property::VerticalScrollbarLayer))
        encoder << node.verticalScrollbarLayer().layerIDForEncoding();
    
    if (node.hasChangedProperty(ScrollingStateNode::Property::ScrollbarHoverState)) {
        auto mouseIsInScrollbar = node.scrollbarHoverState();
        encoder << mouseIsInScrollbar.mouseIsOverHorizontalScrollbar;
        encoder << mouseIsInScrollbar.mouseIsOverVerticalScrollbar;
    }
    
    if (node.hasChangedProperty(ScrollingStateNode::Property::MouseActivityState)) {
        auto mouseLocationState = node.mouseLocationState();
        encoder << mouseLocationState.locationInHorizontalScrollbar;
        encoder << mouseLocationState.locationInVerticalScrollbar;
    }

    if (node.hasChangedProperty(ScrollingStateNode::Property::ScrollbarEnabledState)) {
        auto scrollbarEnabledState = node.scrollbarEnabledState();
        encoder << scrollbarEnabledState.horizontalScrollbarIsEnabled;
        encoder << scrollbarEnabledState.verticalScrollbarIsEnabled;
    }
}

void ArgumentCoder<ScrollingStateFrameScrollingNode>::encode(Encoder& encoder, const ScrollingStateFrameScrollingNode& node)
{
    encoder << node.isMainFrame();
    encoder << node.scrollingNodeID();
    encodeScrollingStateScrollingNodeShared(encoder, node);

    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::FrameScaleFactor, frameScaleFactor)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::EventTrackingRegion, eventTrackingRegions)
    SCROLLING_NODE_ENCODE_ENUM(ScrollingStateNode::Property::BehaviorForFixedElements, scrollBehaviorForFixedElements)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::HeaderHeight, headerHeight)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::FooterHeight, footerHeight)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::TopContentInset, topContentInset)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::FixedElementsLayoutRelativeToFrame, fixedElementsLayoutRelativeToFrame)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::AsyncFrameOrOverflowScrollingEnabled, asyncFrameOrOverflowScrollingEnabled)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::VisualViewportIsSmallerThanLayoutViewport, visualViewportIsSmallerThanLayoutViewport)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::WheelEventGesturesBecomeNonBlocking, wheelEventGesturesBecomeNonBlocking)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::ScrollingPerformanceTestingEnabled, scrollingPerformanceTestingEnabled)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::LayoutViewport, layoutViewport)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::MinLayoutViewportOrigin, minLayoutViewportOrigin)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::MaxLayoutViewportOrigin, maxLayoutViewportOrigin)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::OverrideVisualViewportSize, overrideVisualViewportSize)
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::OverlayScrollbarsEnabled, overlayScrollbarsEnabled)

    if (node.hasChangedProperty(ScrollingStateNode::Property::CounterScrollingLayer))
        encoder << node.counterScrollingLayer().layerIDForEncoding();

    if (node.hasChangedProperty(ScrollingStateNode::Property::InsetClipLayer))
        encoder << node.insetClipLayer().layerIDForEncoding();

    if (node.hasChangedProperty(ScrollingStateNode::Property::ContentShadowLayer))
        encoder << node.contentShadowLayer().layerIDForEncoding();

    if (node.hasChangedProperty(ScrollingStateNode::Property::RootContentsLayer))
        encoder << node.rootContentsLayer().layerIDForEncoding();
}

void ArgumentCoder<ScrollingStateFrameHostingNode>::encode(Encoder& encoder, const ScrollingStateFrameHostingNode& node)
{
    encoder << node.scrollingNodeID();
    encodeNodeShared(encoder, node);
}

void ArgumentCoder<ScrollingStateOverflowScrollingNode>::encode(Encoder& encoder, const ScrollingStateOverflowScrollingNode& node)
{
    encoder << node.scrollingNodeID();
    encodeScrollingStateScrollingNodeShared(encoder, node);
}

void ArgumentCoder<ScrollingStateOverflowScrollProxyNode>::encode(Encoder& encoder, const ScrollingStateOverflowScrollProxyNode& node)
{
    encoder << node.scrollingNodeID();
    encodeNodeShared(encoder, node);
    SCROLLING_NODE_ENCODE(ScrollingStateNode::Property::OverflowScrollingNode, overflowScrollingNode)
}

static bool decodeScrollingStateScrollingNodeShared(Decoder& decoder, ScrollingStateScrollingNode& node)
{
    if (!decodeNodeShared(decoder, node))
        return false;

    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::ScrollableAreaSize, FloatSize, setScrollableAreaSize);
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::TotalContentsSize, FloatSize, setTotalContentsSize);
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::ReachableContentsSize, FloatSize, setReachableContentsSize);
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::ScrollPosition, FloatPoint, setScrollPosition);
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::ScrollOrigin, IntPoint, setScrollOrigin);
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::SnapOffsetsInfo, FloatScrollSnapOffsetsInfo, setSnapOffsetsInfo);
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::CurrentHorizontalSnapOffsetIndex, std::optional<unsigned>, setCurrentHorizontalSnapPointIndex);
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::CurrentVerticalSnapOffsetIndex, std::optional<unsigned>, setCurrentVerticalSnapPointIndex);
#if ENABLE(SCROLLING_THREAD)
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::ReasonsForSynchronousScrolling, OptionSet<SynchronousScrollingReason>, setSynchronousScrollingReasons)
#endif
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::IsMonitoringWheelEvents, bool, setIsMonitoringWheelEvents);
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::ScrollableAreaParams, ScrollableAreaParameters, setScrollableAreaParameters);
    if (node.hasChangedProperty(ScrollingStateNode::Property::RequestedScrollPosition)) {
        RequestedScrollData requestedScrollData;
        if (!decoder.decode(requestedScrollData))
            return false;
        node.setRequestedScrollData(WTFMove(requestedScrollData), ScrollingStateScrollingNode::CanMergeScrollData::No);
    }
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::KeyboardScrollData, RequestedKeyboardScrollData, setKeyboardScrollData);
    SCROLLING_NODE_DECODE_REFERENCE(ScrollingStateNode::Property::ContentAreaHoverState, bool, setMouseIsOverContentArea);

    if (node.hasChangedProperty(ScrollingStateNode::Property::ScrollContainerLayer)) {
        std::optional<PlatformLayerIdentifier> layerID;
        if (!decoder.decode(layerID))
            return false;
        node.setScrollContainerLayer(layerID.value_or(PlatformLayerIdentifier { }));
    }

    if (node.hasChangedProperty(ScrollingStateNode::Property::ScrolledContentsLayer)) {
        std::optional<PlatformLayerIdentifier> layerID;
        if (!decoder.decode(layerID))
            return false;
        node.setScrolledContentsLayer(layerID.value_or(PlatformLayerIdentifier { }));
    }

    if (node.hasChangedProperty(ScrollingStateNode::Property::HorizontalScrollbarLayer)) {
        std::optional<PlatformLayerIdentifier> layerID;
        if (!decoder.decode(layerID))
            return false;
        node.setHorizontalScrollbarLayer(layerID.value_or(PlatformLayerIdentifier { }));
    }

    if (node.hasChangedProperty(ScrollingStateNode::Property::VerticalScrollbarLayer)) {
        std::optional<PlatformLayerIdentifier> layerID;
        if (!decoder.decode(layerID))
            return false;
        node.setVerticalScrollbarLayer(layerID.value_or(PlatformLayerIdentifier { }));
    }
    
    if (node.hasChangedProperty(ScrollingStateNode::Property::ScrollbarHoverState)) {
        bool didEnterScrollbarHorizontal;
        if (!decoder.decode(didEnterScrollbarHorizontal))
            return false;

        bool didEnterScrollbarVertical;
        if (!decoder.decode(didEnterScrollbarVertical))
            return false;
        node.setScrollbarHoverState({ didEnterScrollbarHorizontal, didEnterScrollbarVertical });
    }
    
    if (node.hasChangedProperty(ScrollingStateNode::Property::MouseActivityState)) {
        IntPoint locationInHorizontalScrollbar;
        if (!decoder.decode(locationInHorizontalScrollbar))
            return false;

        IntPoint locationInVerticalScrollbar;
        if (!decoder.decode(locationInVerticalScrollbar))
            return false;
        node.setMouseMovedInContentArea({ locationInHorizontalScrollbar, locationInVerticalScrollbar });
    }

    if (node.hasChangedProperty(ScrollingStateNode::Property::ScrollbarEnabledState)) {
        bool horizontalScrollbarEnabled;
        if (!decoder.decode(horizontalScrollbarEnabled))
            return false;

        bool verticalScrollbarEnabled;
        if (!decoder.decode(verticalScrollbarEnabled))
            return false;
        node.setScrollbarEnabledState(ScrollbarOrientation::Horizontal, horizontalScrollbarEnabled);
        node.setScrollbarEnabledState(ScrollbarOrientation::Vertical, verticalScrollbarEnabled);
    }

    return true;
}

std::optional<Ref<ScrollingStateFrameScrollingNode>> ArgumentCoder<ScrollingStateFrameScrollingNode>::decode(Decoder& decoder)
{
    auto mainFrame = decoder.decode<bool>();
    if (!mainFrame)
        return std::nullopt;

    auto nodeID = decoder.decode<ScrollingNodeID>();
    if (!nodeID)
        return std::nullopt;
    auto node = ScrollingStateFrameScrollingNode::create(*mainFrame, *nodeID);

    if (!decodeScrollingStateScrollingNodeShared(decoder, node))
        return std::nullopt;

    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::FrameScaleFactor, float, setFrameScaleFactor);
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::EventTrackingRegion, EventTrackingRegions, setEventTrackingRegions);
    SCROLLING_NODE_DECODE_ENUM(ScrollingStateNode::Property::BehaviorForFixedElements, ScrollBehaviorForFixedElements, setScrollBehaviorForFixedElements);

    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::HeaderHeight, int, setHeaderHeight);
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::FooterHeight, int, setFooterHeight);
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::TopContentInset, float, setTopContentInset);
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::FixedElementsLayoutRelativeToFrame, bool, setFixedElementsLayoutRelativeToFrame);
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::AsyncFrameOrOverflowScrollingEnabled, bool, setAsyncFrameOrOverflowScrollingEnabled);
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::VisualViewportIsSmallerThanLayoutViewport, bool, setVisualViewportIsSmallerThanLayoutViewport);
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::WheelEventGesturesBecomeNonBlocking, bool, setWheelEventGesturesBecomeNonBlocking)
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::ScrollingPerformanceTestingEnabled, bool, setScrollingPerformanceTestingEnabled)
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::LayoutViewport, FloatRect, setLayoutViewport)
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::MinLayoutViewportOrigin, FloatPoint, setMinLayoutViewportOrigin)
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::MaxLayoutViewportOrigin, FloatPoint, setMaxLayoutViewportOrigin)
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::OverrideVisualViewportSize, std::optional<FloatSize>, setOverrideVisualViewportSize)
    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::OverlayScrollbarsEnabled, bool, setOverlayScrollbarsEnabled)

    if (node->hasChangedProperty(ScrollingStateNode::Property::CounterScrollingLayer)) {
        std::optional<PlatformLayerIdentifier> layerID;
        if (!decoder.decode(layerID))
            return std::nullopt;
        node->setCounterScrollingLayer(layerID.value_or(PlatformLayerIdentifier { }));
    }

    if (node->hasChangedProperty(ScrollingStateNode::Property::InsetClipLayer)) {
        std::optional<PlatformLayerIdentifier> layerID;
        if (!decoder.decode(layerID))
            return std::nullopt;
        node->setInsetClipLayer(layerID.value_or(PlatformLayerIdentifier { }));
    }

    if (node->hasChangedProperty(ScrollingStateNode::Property::ContentShadowLayer)) {
        std::optional<PlatformLayerIdentifier> layerID;
        if (!decoder.decode(layerID))
            return std::nullopt;
        node->setContentShadowLayer(layerID.value_or(PlatformLayerIdentifier { }));
    }

    if (node->hasChangedProperty(ScrollingStateNode::Property::RootContentsLayer)) {
        std::optional<PlatformLayerIdentifier> layerID;
        if (!decoder.decode(layerID))
            return std::nullopt;
        node->setRootContentsLayer(layerID.value_or(PlatformLayerIdentifier { }));
    }

    return WTFMove(node);
}

std::optional<Ref<ScrollingStateFrameHostingNode>> ArgumentCoder<ScrollingStateFrameHostingNode>::decode(Decoder& decoder)
{
    auto nodeID = decoder.decode<ScrollingNodeID>();
    if (!nodeID)
        return std::nullopt;
    auto node = ScrollingStateFrameHostingNode::create(*nodeID);

    if (!decodeNodeShared(decoder, node))
        return std::nullopt;

    return WTFMove(node);
}

std::optional<Ref<ScrollingStateOverflowScrollingNode>> ArgumentCoder<ScrollingStateOverflowScrollingNode>::decode(Decoder& decoder)
{
    auto nodeID = decoder.decode<ScrollingNodeID>();
    if (!nodeID)
        return std::nullopt;
    auto node = ScrollingStateOverflowScrollingNode::create(*nodeID);

    if (!decodeScrollingStateScrollingNodeShared(decoder, node))
        return std::nullopt;

    return WTFMove(node);
}

std::optional<Ref<ScrollingStateOverflowScrollProxyNode>> ArgumentCoder<ScrollingStateOverflowScrollProxyNode>::decode(Decoder& decoder)
{
    auto nodeID = decoder.decode<ScrollingNodeID>();
    if (!nodeID)
        return std::nullopt;
    auto node = ScrollingStateOverflowScrollProxyNode::create(*nodeID);

    if (!decodeNodeShared(decoder, node))
        return std::nullopt;

    SCROLLING_NODE_DECODE(ScrollingStateNode::Property::OverflowScrollingNode, ScrollingNodeID, setOverflowScrollingNode);
    return WTFMove(node);
}

void ArgumentCoder<ScrollingStateFixedNode>::encode(Encoder& encoder, const ScrollingStateFixedNode& node)
{
    encoder << node.scrollingNodeID();
    encodeNodeShared(encoder, node);

    if (node.hasChangedProperty(ScrollingStateNode::Property::ViewportConstraints))
        encoder << node.viewportConstraints();
}

std::optional<Ref<ScrollingStateFixedNode>> ArgumentCoder<ScrollingStateFixedNode>::decode(Decoder& decoder)
{
    auto nodeID = decoder.decode<ScrollingNodeID>();
    if (!nodeID)
        return std::nullopt;
    auto node = ScrollingStateFixedNode::create(*nodeID);

    if (!decodeNodeShared(decoder, node))
        return std::nullopt;

    if (node->hasChangedProperty(ScrollingStateNode::Property::ViewportConstraints)) {
        FixedPositionViewportConstraints decodedValue;
        if (!decoder.decode(decodedValue))
            return std::nullopt;
        node->updateConstraints(decodedValue);
    }

    return WTFMove(node);
}

void ArgumentCoder<ScrollingStateStickyNode>::encode(Encoder& encoder, const ScrollingStateStickyNode& node)
{
    encoder << node.scrollingNodeID();
    encodeNodeShared(encoder, node);

    if (node.hasChangedProperty(ScrollingStateNode::Property::ViewportConstraints))
        encoder << node.viewportConstraints();
}

std::optional<Ref<ScrollingStateStickyNode>> ArgumentCoder<ScrollingStateStickyNode>::decode(Decoder& decoder)
{
    auto nodeID = decoder.decode<ScrollingNodeID>();
    if (!nodeID)
        return std::nullopt;
    auto node = ScrollingStateStickyNode::create(*nodeID);

    if (!decodeNodeShared(decoder, node))
        return std::nullopt;

    if (node->hasChangedProperty(ScrollingStateNode::Property::ViewportConstraints)) {
        StickyPositionViewportConstraints decodedValue;
        if (!decoder.decode(decodedValue))
            return std::nullopt;
        node->updateConstraints(decodedValue);
    }

    return WTFMove(node);
}

void ArgumentCoder<ScrollingStatePositionedNode>::encode(Encoder& encoder, const ScrollingStatePositionedNode& node)
{
    encoder << node.scrollingNodeID();
    encodeNodeShared(encoder, node);

    if (node.hasChangedProperty(ScrollingStateNode::Property::RelatedOverflowScrollingNodes))
        encoder << node.relatedOverflowScrollingNodes();

    if (node.hasChangedProperty(ScrollingStateNode::Property::LayoutConstraintData))
        encoder << node.layoutConstraints();
}

std::optional<Ref<ScrollingStatePositionedNode>> ArgumentCoder<ScrollingStatePositionedNode>::decode(Decoder& decoder)
{
    auto nodeID = decoder.decode<ScrollingNodeID>();
    if (!nodeID)
        return std::nullopt;
    auto node = ScrollingStatePositionedNode::create(*nodeID);

    if (!decodeNodeShared(decoder, node))
        return std::nullopt;

    if (node->hasChangedProperty(ScrollingStateNode::Property::RelatedOverflowScrollingNodes)) {
        Vector<ScrollingNodeID> decodedValue;
        if (!decoder.decode(decodedValue))
            return std::nullopt;
        node->setRelatedOverflowScrollingNodes(WTFMove(decodedValue));
    }

    if (node->hasChangedProperty(ScrollingStateNode::Property::LayoutConstraintData)) {
        AbsolutePositionConstraints decodedValue;
        if (!decoder.decode(decodedValue))
            return std::nullopt;
        node->updateConstraints(decodedValue);
    }

    return WTFMove(node);
}

void ArgumentCoder<WebCore::ScrollingStateTree>::encode(Encoder& encoder, const WebCore::ScrollingStateTree& tree)
{
    encoder << tree.hasNewRootStateNode();
    encoder << tree.hasChangedProperties();
    encoder << tree.rootStateNode();
}

std::optional<WebCore::ScrollingStateTree> ArgumentCoder<WebCore::ScrollingStateTree>::decode(Decoder& decoder)
{
    bool hasNewRootNode;
    if (!decoder.decode(hasNewRootNode))
        return std::nullopt;

    bool hasChangedProperties;
    if (!decoder.decode(hasChangedProperties))
        return std::nullopt;

    ScrollingStateTree scrollingStateTree;
    scrollingStateTree.setHasChangedProperties(hasChangedProperties);

    auto rootNode = decoder.decode<RefPtr<ScrollingStateFrameScrollingNode>>();
    if (!rootNode)
        return std::nullopt;
    if (!scrollingStateTree.setRootStateNodeAfterReconstruction(WTFMove(*rootNode)))
        return std::nullopt;

    scrollingStateTree.setHasNewRootStateNode(hasNewRootNode);

    return { WTFMove(scrollingStateTree) };
}

namespace WebKit {

RemoteScrollingCoordinatorTransaction::RemoteScrollingCoordinatorTransaction() = default;

RemoteScrollingCoordinatorTransaction::RemoteScrollingCoordinatorTransaction(std::unique_ptr<WebCore::ScrollingStateTree>&& scrollingStateTree, bool clearScrollLatching, FromDeserialization fromDeserialization)
    : m_scrollingStateTree(WTFMove(scrollingStateTree))
    , m_clearScrollLatching(clearScrollLatching)
{
    if (!m_scrollingStateTree)
        m_scrollingStateTree = makeUnique<WebCore::ScrollingStateTree>();
    if (fromDeserialization == FromDeserialization::Yes)
        m_scrollingStateTree->attachDeserializedNodes();
}

RemoteScrollingCoordinatorTransaction::RemoteScrollingCoordinatorTransaction(RemoteScrollingCoordinatorTransaction&&) = default;

RemoteScrollingCoordinatorTransaction& RemoteScrollingCoordinatorTransaction::operator=(RemoteScrollingCoordinatorTransaction&&) = default;

RemoteScrollingCoordinatorTransaction::~RemoteScrollingCoordinatorTransaction() = default;

#if !defined(NDEBUG) || !LOG_DISABLED

static void dump(TextStream& ts, const ScrollingStateScrollingNode& node, bool changedPropertiesOnly)
{
    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::ScrollableAreaSize))
        ts.dumpProperty("scrollable-area-size", node.scrollableAreaSize());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::TotalContentsSize))
        ts.dumpProperty("total-contents-size", node.totalContentsSize());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::ReachableContentsSize))
        ts.dumpProperty("reachable-contents-size", node.reachableContentsSize());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::ScrollPosition))
        ts.dumpProperty("scroll-position", node.scrollPosition());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::ScrollOrigin))
        ts.dumpProperty("scroll-origin", node.scrollOrigin());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::RequestedScrollPosition)) {
        const auto& requestedScrollData = node.requestedScrollData();
        ts.dumpProperty("requested-type", requestedScrollData.requestType);
        if (requestedScrollData.requestType != ScrollRequestType::CancelAnimatedScroll) {
            if (requestedScrollData.requestType == ScrollRequestType::DeltaUpdate)
                ts.dumpProperty("requested-scroll-delta", std::get<FloatSize>(requestedScrollData.scrollPositionOrDelta));
            else
                ts.dumpProperty("requested-scroll-position", std::get<FloatPoint>(requestedScrollData.scrollPositionOrDelta));

            ts.dumpProperty("requested-scroll-position-is-programatic", requestedScrollData.scrollType);
            ts.dumpProperty("requested-scroll-position-clamping", requestedScrollData.clamping);
            ts.dumpProperty("requested-scroll-position-animated", requestedScrollData.animated == ScrollIsAnimated::Yes);
        }
    }

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::ScrollContainerLayer))
        ts.dumpProperty("scroll-container-layer", static_cast<PlatformLayerIdentifier>(node.scrollContainerLayer()));

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::ScrolledContentsLayer))
        ts.dumpProperty("scrolled-contents-layer", static_cast<PlatformLayerIdentifier>(node.scrolledContentsLayer()));

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::SnapOffsetsInfo)) {
        ts.dumpProperty("horizontal snap offsets", node.snapOffsetsInfo().horizontalSnapOffsets);
        ts.dumpProperty("vertical snap offsets", node.snapOffsetsInfo().verticalSnapOffsets);
        ts.dumpProperty("current horizontal snap point index", node.currentHorizontalSnapPointIndex());
        ts.dumpProperty("current vertical snap point index", node.currentVerticalSnapPointIndex());
    }

#if ENABLE(SCROLLING_THREAD)
    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::ReasonsForSynchronousScrolling))
        ts.dumpProperty("synchronous scrolling reasons", node.synchronousScrollingReasons());
#endif

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::IsMonitoringWheelEvents))
        ts.dumpProperty("is monitoring wheel events", node.isMonitoringWheelEvents());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::KeyboardScrollData)) {
        const auto& keyboardScrollData = node.keyboardScrollData();
        if (keyboardScrollData.action == KeyboardScrollAction::StartAnimation && keyboardScrollData.keyboardScroll) {
            ts.dumpProperty("keyboard-scroll-data-action", "start animation");

            ts.dumpProperty("keyboard-scroll-data-scroll-offset", keyboardScrollData.keyboardScroll->offset);
            ts.dumpProperty("keyboard-scroll-data-scroll-maximum-velocity", keyboardScrollData.keyboardScroll->maximumVelocity);
            ts.dumpProperty("keyboard-scroll-data-scroll-force", keyboardScrollData.keyboardScroll->force);
            ts.dumpProperty("keyboard-scroll-data-scroll-granularity", keyboardScrollData.keyboardScroll->granularity);
            ts.dumpProperty("keyboard-scroll-data-scroll-direction", keyboardScrollData.keyboardScroll->direction);
        } else if (keyboardScrollData.action == KeyboardScrollAction::StopWithAnimation)
            ts.dumpProperty("keyboard-scroll-data-action", "stop with animation");
        else if (keyboardScrollData.action == KeyboardScrollAction::StopImmediately)
            ts.dumpProperty("keyboard-scroll-data-action", "stop immediately");
    }
}

static void dump(TextStream& ts, const ScrollingStateFrameHostingNode& node, bool changedPropertiesOnly)
{
}

static void dump(TextStream& ts, const ScrollingStateFrameScrollingNode& node, bool changedPropertiesOnly)
{
    dump(ts, static_cast<const ScrollingStateScrollingNode&>(node), changedPropertiesOnly);
    
    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::FrameScaleFactor))
        ts.dumpProperty("frame-scale-factor", node.frameScaleFactor());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::EventTrackingRegion)) {
        {
            TextStream::GroupScope group(ts);
            ts << "asynchronous-event-tracking-region";
            for (auto rect : node.eventTrackingRegions().asynchronousDispatchRegion.rects()) {
                ts << "\n";
                ts.writeIndent();
                ts << rect;
            }
        }
        for (const auto& synchronousEventRegion : node.eventTrackingRegions().eventSpecificSynchronousDispatchRegions) {
            TextStream::GroupScope group(ts);
            ts << "synchronous-event-tracking-region for event " << EventTrackingRegions::eventName(synchronousEventRegion.key);

            for (auto rect : synchronousEventRegion.value.rects()) {
                ts << "\n";
                ts.writeIndent();
                ts << rect;
            }
        }
    }

    // FIXME: dump scrollableAreaParameters
    // FIXME: dump scrollBehaviorForFixedElements

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::HeaderHeight))
        ts.dumpProperty("header-height", node.headerHeight());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::FooterHeight))
        ts.dumpProperty("footer-height", node.footerHeight());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::TopContentInset))
        ts.dumpProperty("top-content-inset", node.topContentInset());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::FrameScaleFactor))
        ts.dumpProperty("frame-scale-factor", node.frameScaleFactor());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::InsetClipLayer))
        ts.dumpProperty("clip-inset-layer", static_cast<PlatformLayerIdentifier>(node.insetClipLayer()));

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::ContentShadowLayer))
        ts.dumpProperty("content-shadow-layer", static_cast<PlatformLayerIdentifier>(node.contentShadowLayer()));

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::HeaderLayer))
        ts.dumpProperty("header-layer", static_cast<PlatformLayerIdentifier>(node.headerLayer()));

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::FooterLayer))
        ts.dumpProperty("footer-layer", static_cast<PlatformLayerIdentifier>(node.footerLayer()));
}
    
static void dump(TextStream& ts, const ScrollingStateOverflowScrollingNode& node, bool changedPropertiesOnly)
{
    dump(ts, static_cast<const ScrollingStateScrollingNode&>(node), changedPropertiesOnly);
}

static void dump(TextStream& ts, const ScrollingStateOverflowScrollProxyNode& node, bool changedPropertiesOnly)
{
    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::OverflowScrollingNode))
        ts.dumpProperty("overflow-scrolling-node", node.overflowScrollingNode());
}

static void dump(TextStream& ts, const ScrollingStateFixedNode& node, bool changedPropertiesOnly)
{
    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::ViewportConstraints))
        ts << node.viewportConstraints();
}

static void dump(TextStream& ts, const ScrollingStateStickyNode& node, bool changedPropertiesOnly)
{
    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::ViewportConstraints))
        ts << node.viewportConstraints();
}

static void dump(TextStream& ts, const ScrollingStatePositionedNode& node, bool changedPropertiesOnly)
{
    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::RelatedOverflowScrollingNodes))
        ts << node.relatedOverflowScrollingNodes();

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::LayoutConstraintData))
        ts << node.layoutConstraints();
}

static void dump(TextStream& ts, const ScrollingStateNode& node, bool changedPropertiesOnly)
{
    ts.dumpProperty("type", node.nodeType());

    if (!changedPropertiesOnly || node.hasChangedProperty(ScrollingStateNode::Property::Layer))
        ts.dumpProperty("layer", static_cast<PlatformLayerIdentifier>(node.layer()));
    
    switch (node.nodeType()) {
    case ScrollingNodeType::MainFrame:
    case ScrollingNodeType::Subframe:
        dump(ts, downcast<ScrollingStateFrameScrollingNode>(node), changedPropertiesOnly);
        break;
    case ScrollingNodeType::FrameHosting:
        dump(ts, downcast<ScrollingStateFrameHostingNode>(node), changedPropertiesOnly);
        break;
    case ScrollingNodeType::Overflow:
        dump(ts, downcast<ScrollingStateOverflowScrollingNode>(node), changedPropertiesOnly);
        break;
    case ScrollingNodeType::OverflowProxy:
        dump(ts, downcast<ScrollingStateOverflowScrollProxyNode>(node), changedPropertiesOnly);
        break;
    case ScrollingNodeType::Fixed:
        dump(ts, downcast<ScrollingStateFixedNode>(node), changedPropertiesOnly);
        break;
    case ScrollingNodeType::Sticky:
        dump(ts, downcast<ScrollingStateStickyNode>(node), changedPropertiesOnly);
        break;
    case ScrollingNodeType::Positioned:
        dump(ts, downcast<ScrollingStatePositionedNode>(node), changedPropertiesOnly);
        break;
    }
}

static void recursiveDumpNodes(TextStream& ts, const ScrollingStateNode& node, bool changedPropertiesOnly)
{
    TextStream::GroupScope group(ts);
    ts << "node " << node.scrollingNodeID();
    dump(ts, node, changedPropertiesOnly);

    if (!node.children().isEmpty()) {
        TextStream::GroupScope group(ts);
        ts << "children";

        for (auto& childNode : node.children())
            recursiveDumpNodes(ts, childNode, changedPropertiesOnly);
    }
}

static void dump(TextStream& ts, const ScrollingStateTree& stateTree, bool changedPropertiesOnly)
{
    ts.dumpProperty("has changed properties", stateTree.hasChangedProperties());
    ts.dumpProperty("has new root node", stateTree.hasNewRootStateNode());

    if (stateTree.rootStateNode())
        recursiveDumpNodes(ts, Ref { *stateTree.rootStateNode() }, changedPropertiesOnly);
}

String RemoteScrollingCoordinatorTransaction::description() const
{
    TextStream ts;

    if (m_clearScrollLatching)
        ts.dumpProperty("clear scroll latching", clearScrollLatching());

    ts.startGroup();
    ts << "scrolling state tree";

    if (m_scrollingStateTree) {
        if (!m_scrollingStateTree->hasChangedProperties())
            ts << " - no changes";
        else
            WebKit::dump(ts, *m_scrollingStateTree.get(), true);
    } else
        ts << " - none";

    ts.endGroup();

    return ts.release();
}

void RemoteScrollingCoordinatorTransaction::dump() const
{
    WTFLogAlways("%s", description().utf8().data());
}
#endif // !defined(NDEBUG) || !LOG_DISABLED

} // namespace WebKit

#endif // ENABLE(UI_SIDE_COMPOSITING)
