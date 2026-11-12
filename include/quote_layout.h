// Column geometry for the price table.
//
// Kept free of TFT / Arduino dependencies so the layout decisions (including
// dropping the volume column when the screen is too narrow) can be tested on a
// development machine. The caller measures the strings with the active font and
// passes the widths in.

#pragma once

#include <stddef.h>
#include <string.h>

namespace quotes {

// Screen-edge padding and spacing between columns.
constexpr int kLayoutMargin = 4;
constexpr int kLayoutGap = 7;
constexpr int kLayoutMinGap = 4;
constexpr int kLayoutMaxGap = 28;

// ---- Vertical geometry ----------------------------------------------------
//
// From the top of the screen down: a timestamp/status bar, a gap, the column
// labels, a gap, a divider line, then the rows. The gaps around the labels are
// deliberate: labels that sit tight against the bar above them read as part of
// it. These are shared with the display code so the spacing is one definition.
constexpr int kStatusHeight = 20;   // timestamp / status bar
constexpr int kHeaderPadTop = 8;    // air between the bar and the labels
constexpr int kHeaderPadBottom = 5; // air between the labels and the divider
constexpr int kDividerToRows = 2;   // air below the divider before row 1
constexpr int kRowGap = 6;          // air between rows, on top of the font
constexpr int kBottomMargin = 3;

// Centre of the header label text, given the font's line height.
constexpr int headerCentreY(int textHeight) {
    return kStatusHeight + kHeaderPadTop + textHeight / 2;
}

// y of the rule under the header labels.
constexpr int headerDividerY(int textHeight) {
    return kStatusHeight + kHeaderPadTop + textHeight + kHeaderPadBottom;
}

// y of the top of the first row.
constexpr int rowsTopY(int textHeight) {
    return headerDividerY(textHeight) + kDividerToRows;
}

struct Layout {
    int nameX;      // left edge of the coin name column
    int nameWidth;  // pixels available for the name; 0 if there is no room
    int priceRight; // right edge for the right-aligned price
    int changeRight;
    int volumeRight; // 0 when the volume column is hidden
    bool showVolume;
    int gap;
};

// Lays out four columns (`name`, `price`, `change`, `volume`) inside `width`.
//
// Columns are anchored from the right edge so the numbers always line up on
// their last digit. Comfortable spacing is preferred over the extra column: the
// volume column is only shown when the other three columns and the volume fit
// with the normal gap. Otherwise it is dropped. Spare width is shared out
// between the columns (up to kLayoutMaxGap) instead of leaving one column
// stretched, and if space is really tight the gap is tightened and the name
// budget shrinks to whatever is left (the caller truncates the text).
inline Layout computeLayout(int width, int nameWidth, int priceWidth,
                            int changeWidth, int volumeWidth) {
    const int contentWithoutVolume = nameWidth + priceWidth + changeWidth;
    const int contentWithVolume = contentWithoutVolume + volumeWidth;

    Layout layout{};
    layout.showVolume =
        kLayoutMargin * 2 + contentWithVolume + kLayoutGap * 3 <= width;

    const int columns = layout.showVolume ? 4 : 3;
    const int gaps = columns - 1;
    const int content = layout.showVolume ? contentWithVolume : contentWithoutVolume;
    const int requiredWithNormalGap =
        kLayoutMargin * 2 + content + kLayoutGap * gaps;

    if (requiredWithNormalGap <= width) {
        // Share the spare width evenly between the columns.
        layout.gap = kLayoutGap + (width - requiredWithNormalGap) / gaps;
        if (layout.gap > kLayoutMaxGap)
            layout.gap = kLayoutMaxGap;
    } else {
        layout.gap = kLayoutMinGap;
    }

    if (layout.showVolume) {
        layout.volumeRight = width - kLayoutMargin;
        layout.changeRight = layout.volumeRight - volumeWidth - layout.gap;
    } else {
        layout.volumeRight = 0;
        layout.changeRight = width - kLayoutMargin;
    }

    layout.priceRight = layout.changeRight - changeWidth - layout.gap;
    layout.nameX = kLayoutMargin;
    layout.nameWidth = layout.priceRight - priceWidth - layout.gap - kLayoutMargin;
    if (layout.nameWidth < 0)
        layout.nameWidth = 0;

    return layout;
}

// How many rows of `rowHeight` pixels fit between `top` and the bottom margin.
// The display code uses this so rows are never drawn off the screen.
inline int rowCapacity(int height, int top, int rowHeight, int bottomMargin) {
    if (rowHeight <= 0)
        return 0;
    const int available = height - top - bottomMargin;
    return available > 0 ? available / rowHeight : 0;
}

// Copies `src` into `dst` and, if the copy is wider than `maxWidth` as measured
// by `measure`, shortens it until it fits and marks the cut with "..".
//
// Writes at most `dstSize` bytes, including the terminator, whatever the input
// length and however small `dstSize` is.
template <typename Measure>
inline void fitTextToWidth(char *dst, size_t dstSize, const char *src,
                           int maxWidth, Measure measure) {
    if (dstSize == 0)
        return;

    size_t len = src ? strlen(src) : 0;
    if (len > dstSize - 1)
        len = dstSize - 1;
    if (len > 0)
        memcpy(dst, src, len);
    dst[len] = '\0';

    if (len == 0 || measure(dst) <= maxWidth)
        return;

    char candidate[32];
    const size_t bufferLimit = sizeof(candidate) - 3; // room for ".." and NUL
    const size_t dstLimit = dstSize > 3 ? dstSize - 3 : 0;
    size_t keep = len;
    if (keep > bufferLimit)
        keep = bufferLimit;
    if (keep > dstLimit)
        keep = dstLimit;

    for (; keep > 0; keep--) {
        memcpy(candidate, dst, keep);
        candidate[keep] = '.';
        candidate[keep + 1] = '.';
        candidate[keep + 2] = '\0';
        if (measure(candidate) <= maxWidth) {
            memcpy(dst, candidate, keep + 3);
            return;
        }
    }

    dst[0] = '\0'; // not even ".." fits
}

}  // namespace quotes
