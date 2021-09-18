/*
* Copyright (C) 2020 Apple Inc. All rights reserved.
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
#include "LayoutIntegrationInlineContentBuilder.h"

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "InlineFormattingState.h"
#include "InlineLineRun.h"
#include "LayoutBoxGeometry.h"
#include "LayoutIntegrationBoxTree.h"
#include "LayoutIntegrationInlineContent.h"
#include "LayoutReplacedBox.h"
#include "LayoutState.h"
#include "RenderBlockFlow.h"
#include "StringTruncator.h"

namespace WebCore {
namespace LayoutIntegration {

inline Layout::LineGeometry::EnclosingTopAndBottom operator+(const Layout::LineGeometry::EnclosingTopAndBottom enclosingTopAndBottom, float offset)
{
    return { enclosingTopAndBottom.top + offset, enclosingTopAndBottom.bottom + offset };
}

inline static float lineOverflowWidth(const RenderBlockFlow& flow, Layout::InlineLayoutUnit lineContentLogicalWidth)
{
    // FIXME: It's the copy of the lets-adjust-overflow-for-the-caret behavior from LegacyLineLayout::addOverflowFromInlineChildren.
    auto endPadding = flow.hasNonVisibleOverflow() ? flow.paddingEnd() : 0_lu;
    if (!endPadding)
        endPadding = flow.endPaddingWidthForCaret();
    if (flow.hasNonVisibleOverflow() && !endPadding && flow.element() && flow.element()->isRootEditableElement())
        endPadding = 1;
    return lineContentLogicalWidth + endPadding;
}

InlineContentBuilder::InlineContentBuilder(const RenderBlockFlow& blockFlow, const BoxTree& boxTree)
    : m_blockFlow(blockFlow)
    , m_boxTree(boxTree)
{
}

void InlineContentBuilder::build(Layout::InlineFormattingState& inlineFormattingState, InlineContent& inlineContent) const
{
    // FIXME: This might need a different approach with partial layout where the layout code needs to know about the runs.
    inlineContent.runs = WTFMove(inlineFormattingState.runs());
    createDisplayLines(inlineFormattingState, inlineContent);
}

void InlineContentBuilder::createDisplayLines(Layout::InlineFormattingState& inlineFormattingState, InlineContent& inlineContent) const
{
    auto& lines = inlineFormattingState.lines();
    auto& runs = inlineContent.runs;
    size_t runIndex = 0;
    inlineContent.lines.reserveInitialCapacity(lines.size());
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        auto& line = lines[lineIndex];
        auto scrollableOverflowRect = FloatRect { line.scrollableOverflow() };
        if (auto overflowWidth = lineOverflowWidth(m_blockFlow, line.contentLogicalWidth()); overflowWidth > scrollableOverflowRect.width())
            scrollableOverflowRect.setWidth(overflowWidth);

        auto firstRunIndex = runIndex;
        auto lineInkOverflowRect = scrollableOverflowRect;
        // Collect overflow from runs.
        for (; runIndex < runs.size() && runs[runIndex].lineIndex() == lineIndex; ++runIndex) {
            auto& run = runs[runIndex];

            lineInkOverflowRect.unite(run.inkOverflow());

            auto& layoutBox = run.layoutBox();
            if (layoutBox.isReplacedBox()) {
                // Similar to LegacyInlineFlowBox::addReplacedChildOverflow.
                auto& box = downcast<RenderBox>(m_boxTree.rendererForLayoutBox(layoutBox));
                auto runLogicalRect = run.logicalRect();
                if (!box.hasSelfPaintingLayer()) {
                    auto childInkOverflow = box.logicalVisualOverflowRectForPropagation(&box.parent()->style());
                    childInkOverflow.move(runLogicalRect.left(), runLogicalRect.top());
                    lineInkOverflowRect.unite(childInkOverflow);
                }
                auto childScrollableOverflow = box.logicalLayoutOverflowRectForPropagation(&box.parent()->style());
                childScrollableOverflow.move(runLogicalRect.left(), runLogicalRect.top());
                scrollableOverflowRect.unite(childScrollableOverflow);
            }
        }
        auto lineBoxLogicalRect = FloatRect { line.lineBoxLogicalRect() };
        auto runCount = runIndex - firstRunIndex;
        inlineContent.lines.append({ firstRunIndex, runCount, lineBoxLogicalRect, line.enclosingTopAndBottom().top, line.enclosingTopAndBottom().bottom, scrollableOverflowRect, lineInkOverflowRect, line.baseline(), line.contentLogicalLeft(), line.contentLogicalWidth() });
    }
}

}
}

#endif
