// Tests for the column geometry: columns must never overlap, numbers must stay
// anchored to the right edge, spare width must be shared out rather than
// stretching one column, and the volume column must disappear when the payload
// does not fit alongside it.
//
// Run with: pio test -e native

#include <string.h>
#include <unity.h>

#include "quote_layout.h"

namespace {

// Column widths as measured for the README payload on the 240 px panel. Each is
// the wider of the label and the widest value: "COIN" 39 / "BTC" 32,
// "PRICE" 43 / "$28,451" 56, "24H" 29 / "-1.5%" 40, "VOL" 40 / "759M" 40.
constexpr int kNameW = 39;
constexpr int kPriceW = 56;
constexpr int kChangeW = 40;
constexpr int kVolumeW = 40;

void assertColumnsAreOrdered(const quotes::Layout &layout, int priceWidth,
                             int changeWidth, int volumeWidth) {
    // Numbers are anchored to the right edge of their column.
    TEST_ASSERT_EQUAL_INT(layout.changeRight - changeWidth - layout.gap,
                          layout.priceRight);

    if (layout.showVolume) {
        TEST_ASSERT_GREATER_OR_EQUAL(layout.changeRight + layout.gap,
                                     layout.volumeRight - volumeWidth);
    } else {
        TEST_ASSERT_EQUAL_INT(0, layout.volumeRight);
    }

    TEST_ASSERT_GREATER_OR_EQUAL(quotes::kLayoutMinGap, layout.gap);
    TEST_ASSERT_LESS_OR_EQUAL(quotes::kLayoutMaxGap, layout.gap);

    // A visible name column must end before the price column starts.
    if (layout.nameWidth > 0) {
        TEST_ASSERT_LESS_OR_EQUAL(layout.priceRight - priceWidth,
                                  layout.nameX + layout.nameWidth + layout.gap);
    }
}

}  // namespace

// The real panel: everything fits, the spare 43 px is shared out as ~21 px
// gaps, and each column is just wide enough for its widest value.
static void testRealPanelSharesSpareWidth() {
    const quotes::Layout layout =
        quotes::computeLayout(240, kNameW, kPriceW, kChangeW, kVolumeW);

    TEST_ASSERT_TRUE(layout.showVolume);
    TEST_ASSERT_EQUAL_INT(19, layout.gap);
    TEST_ASSERT_EQUAL_INT(240 - quotes::kLayoutMargin, layout.volumeRight);
    TEST_ASSERT_EQUAL_INT(4, layout.nameX);
    // The name column fits its label exactly (39 px).
    TEST_ASSERT_EQUAL_INT(kNameW, layout.nameWidth);
    assertColumnsAreOrdered(layout, kPriceW, kChangeW, kVolumeW);
}

// A wide panel still caps the gaps instead of spreading the columns absurdly.
static void testWidePanelCapsTheGap() {
    const quotes::Layout layout =
        quotes::computeLayout(320, kNameW, kPriceW, kChangeW, kVolumeW);
    TEST_ASSERT_TRUE(layout.showVolume);
    TEST_ASSERT_EQUAL_INT(quotes::kLayoutMaxGap, layout.gap);
    TEST_ASSERT_EQUAL_INT(320 - quotes::kLayoutMargin, layout.volumeRight);
    assertColumnsAreOrdered(layout, kPriceW, kChangeW, kVolumeW);
}

// One pixel less than the roomy requirement drops the volume column rather than
// squeezing the remaining columns.
static void testVolumeDroppedWhenItNoLongerFits() {
    const int exactly = kNameW + kPriceW + kChangeW + kVolumeW +
                        quotes::kLayoutGap * 3 + quotes::kLayoutMargin * 2;

    const quotes::Layout fits =
        quotes::computeLayout(exactly, kNameW, kPriceW, kChangeW, kVolumeW);
    TEST_ASSERT_TRUE(fits.showVolume);
    TEST_ASSERT_EQUAL_INT(quotes::kLayoutGap, fits.gap);

    const quotes::Layout tooNarrow =
        quotes::computeLayout(exactly - 1, kNameW, kPriceW, kChangeW, kVolumeW);
    TEST_ASSERT_FALSE(tooNarrow.showVolume);
    TEST_ASSERT_EQUAL_INT(0, tooNarrow.volumeRight);
    TEST_ASSERT_EQUAL_INT(exactly - 1 - quotes::kLayoutMargin,
                          tooNarrow.changeRight);
    assertColumnsAreOrdered(tooNarrow, kPriceW, kChangeW, kVolumeW);

    // Without the volume column the name keeps at least its content width.
    TEST_ASSERT_GREATER_OR_EQUAL(kNameW, tooNarrow.nameWidth);
}

// Long prices, changes and names on the real panel: no volume column, tightened
// spacing, and the name column truncated by the caller instead of overlapping.
static void testLongFieldsDropVolumeAndTightenGaps() {
    const quotes::Layout layout = quotes::computeLayout(240, 127, 101, 67, 50);
    TEST_ASSERT_FALSE(layout.showVolume);
    TEST_ASSERT_EQUAL_INT(quotes::kLayoutMinGap, layout.gap);
    TEST_ASSERT_EQUAL_INT(0, layout.volumeRight);
    assertColumnsAreOrdered(layout, 101, 67, 50);
    TEST_ASSERT_GREATER_OR_EQUAL(0, layout.nameWidth);
}

// Pathologically narrow: the name column collapses to nothing and the numeric
// columns stay ordered, so the worst case is clipped text, never overlap.
static void testVeryNarrowScreenCollapsesNameColumn() {
    const quotes::Layout layout = quotes::computeLayout(120, 90, 96, 40, 30);
    TEST_ASSERT_FALSE(layout.showVolume);
    TEST_ASSERT_EQUAL_INT(0, layout.nameWidth);
    TEST_ASSERT_EQUAL_INT(quotes::kLayoutMargin, layout.nameX);
    assertColumnsAreOrdered(layout, 96, 40, 30);
}

// Zero-width payload strings (an empty coin list) must still produce a sane
// layout rather than negative offsets.
static void testEmptyBoardLayoutIsSane() {
    const quotes::Layout layout = quotes::computeLayout(240, 0, 0, 0, 0);
    TEST_ASSERT_TRUE(layout.showVolume);
    TEST_ASSERT_EQUAL_INT(240 - quotes::kLayoutMargin, layout.volumeRight);
    TEST_ASSERT_EQUAL_INT(quotes::kLayoutMargin, layout.nameX);
    TEST_ASSERT_GREATER_THAN(0, layout.nameWidth);
}

// Header spacing: the labels must sit clear of the timestamp bar above them,
// which is what this geometry exists to guarantee.
static void testHeaderHasAirAboveTheLabels() {
    constexpr int kTextHeight = 16; // NotoSansBold15 line height

    const int centre = quotes::headerCentreY(kTextHeight);
    const int labelTop = centre - kTextHeight / 2;
    const int labelBottom = labelTop + kTextHeight;
    const int divider = quotes::headerDividerY(kTextHeight);

    // A visible gap between the bar and the top of the label text...
    TEST_ASSERT_GREATER_OR_EQUAL(6, labelTop - quotes::kStatusHeight);
    // ...and between the bottom of the label text and the divider.
    TEST_ASSERT_GREATER_OR_EQUAL(3, divider - labelBottom);
    // The divider clears the label text.
    TEST_ASSERT_GREATER_THAN(labelBottom, divider);
    // Rows start below the divider.
    TEST_ASSERT_GREATER_OR_EQUAL(divider, quotes::rowsTopY(kTextHeight));
}

// Row capacity: with the real panel and font, twelve rows fit; nothing may be
// drawn past the bottom margin.
static void testRowCapacityFitsRealPanel() {
    constexpr int kTextHeight = 16;
    constexpr int kHeight = 320;

    const int rowHeight = kTextHeight + quotes::kRowGap;
    const int top = quotes::rowsTopY(kTextHeight);
    const int rows = quotes::rowCapacity(kHeight, top, rowHeight,
                                         quotes::kBottomMargin);

    TEST_ASSERT_EQUAL_INT(12, rows);
    // The last row ends inside the bottom margin...
    TEST_ASSERT_LESS_OR_EQUAL(kHeight - quotes::kBottomMargin,
                              top + rows * rowHeight);
    // ...and one more row would not fit.
    TEST_ASSERT_GREATER_THAN(kHeight - quotes::kBottomMargin,
                             top + (rows + 1) * rowHeight);
}

// Text centred in a row band must stay inside that band, so a row never bleeds
// into its neighbours or off the bottom of the screen.
static void testRowTextStaysInsideItsBand() {
    constexpr int kTextHeight = 16;
    constexpr int kHeight = 320;

    const int rowHeight = kTextHeight + quotes::kRowGap;
    const int top = quotes::rowsTopY(kTextHeight);
    const int rows = quotes::rowCapacity(kHeight, top, rowHeight,
                                         quotes::kBottomMargin);

    for (int i = 0; i < rows; i++) {
        const int band = top + i * rowHeight;
        const int centre = band + rowHeight / 2;
        const int cellTop = centre - kTextHeight / 2;
        TEST_ASSERT_GREATER_OR_EQUAL(band, cellTop);
        TEST_ASSERT_LESS_OR_EQUAL(band + rowHeight, cellTop + kTextHeight);
    }
}

static void testRowCapacityDegenerateInputs() {
    TEST_ASSERT_EQUAL_INT(0, quotes::rowCapacity(320, 40, 0, 3));
    TEST_ASSERT_EQUAL_INT(0, quotes::rowCapacity(320, 320, 23, 3));
    TEST_ASSERT_EQUAL_INT(0, quotes::rowCapacity(20, 40, 23, 3));
}


namespace {

// Stand-in for TFT_eSPI's textWidth(): 6 px per character.
int fakeWidth(const char *text) { return static_cast<int>(strlen(text)) * 6; }

// Font-shaped metric: every glyph advances 8 px, except the final glyph which
// contributes its ink width only (as TFT_eSPI does), and '.' which is narrow.
// With a wide final glyph, a ".." replacement can measure narrower than the
// text it replaces, which is what makes an unbounded truncation overflow.
int fontLikeWidth(const char *text) {
    const size_t len = strlen(text);
    int total = 0;
    for (size_t i = 0; i < len; i++) {
        const char c = text[i];
        const bool last = i == len - 1;
        if (last)
            total += c == '.' ? 2 : (c == 'W' ? 15 : 3);
        else
            total += c == '.' ? 2 : 8;
    }
    return total;
}

}  // namespace

// A name that fits is left untouched.
static void testFitTextKeepsShortText() {
    char out[12];
    quotes::fitTextToWidth(out, sizeof(out), "BTC", 6 * 11, fakeWidth);
    TEST_ASSERT_EQUAL_STRING("BTC", out);
}

// A name that is too wide is cut and marked with "..": 11 chars do not fit in
// 8 chars' worth of pixels, so at most 6 characters plus ".." remain.
static void testFitTextTruncatesWithMarker() {
    char out[12];
    quotes::fitTextToWidth(out, sizeof(out), "LONGCOINNAME", 8 * 6, fakeWidth);
    TEST_ASSERT_EQUAL_STRING("LONGCO..", out);
    TEST_ASSERT_LESS_OR_EQUAL(8 * 6, fakeWidth(out));
}

// Regression: truncating a name of exactly buffer size - 1 must never write
// past the buffer when the ".." marker is appended. The destination is heap
// allocated so AddressSanitizer (native env) flags any overrun.
static void testFitTextTruncationStaysInsideNameBuffer() {
    char *out = new char[12];
    quotes::fitTextToWidth(out, 12, "ABCDEFGHIJK", 9 * 6, fakeWidth);
    TEST_ASSERT_EQUAL_STRING("ABCDEFG..", out);
    TEST_ASSERT_EQUAL_UINT(9, strlen(out));
    delete[] out;
}

// No width at all: nothing is drawn, and the buffer still ends in-bounds.
static void testFitTextNoRoomAtAll() {
    char *out = new char[12];
    memset(out, 'Z', 12);
    quotes::fitTextToWidth(out, 12, "ABCDEFGHIJK", 1, fakeWidth);
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_INT('\0', out[11]);
    delete[] out;
}

// Regression: with a font where "text.." can be narrower than "textW", an
// unclamped truncation would copy 13 bytes into a 12 byte name buffer.
static void testFitTextAdversarialMetricStaysInBounds() {
    char *out = new char[12];
    // 10 x 8 px + a 15 px final glyph = 95 px, which does not fit in 90.
    quotes::fitTextToWidth(out, 12, "IIIIIIIIIIW", 90, fontLikeWidth);
    TEST_ASSERT_EQUAL_UINT(11, strlen(out));
    TEST_ASSERT_LESS_OR_EQUAL(90, fontLikeWidth(out));
    TEST_ASSERT_EQUAL_STRING("IIIIIIIII..", out);
    delete[] out;
}

// Just enough room for a couple of characters plus the marker.
static void testFitTextTightButPossible() {
    char out[12];
    quotes::fitTextToWidth(out, sizeof(out), "ABCDEFGHIJK", 4 * 6, fakeWidth);
    TEST_ASSERT_EQUAL_STRING("AB..", out);
}

// Degenerate destinations must not be written to at all.
static void testFitTextZeroSizeDestination() {
    char out[1] = {'Q'};
    quotes::fitTextToWidth(out, 0, "BTC", 0, fakeWidth);
    TEST_ASSERT_EQUAL_INT('Q', out[0]);
}

// Empty input stays empty.
static void testFitTextEmptyInput() {
    char out[12] = "seed";
    quotes::fitTextToWidth(out, sizeof(out), "", 0, fakeWidth);
    TEST_ASSERT_EQUAL_STRING("", out);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(testRealPanelSharesSpareWidth);
    RUN_TEST(testWidePanelCapsTheGap);
    RUN_TEST(testVolumeDroppedWhenItNoLongerFits);
    RUN_TEST(testLongFieldsDropVolumeAndTightenGaps);
    RUN_TEST(testVeryNarrowScreenCollapsesNameColumn);
    RUN_TEST(testEmptyBoardLayoutIsSane);
    RUN_TEST(testHeaderHasAirAboveTheLabels);
    RUN_TEST(testRowCapacityFitsRealPanel);
    RUN_TEST(testRowTextStaysInsideItsBand);
    RUN_TEST(testRowCapacityDegenerateInputs);
    RUN_TEST(testFitTextKeepsShortText);
    RUN_TEST(testFitTextTruncatesWithMarker);
    RUN_TEST(testFitTextTruncationStaysInsideNameBuffer);
    RUN_TEST(testFitTextNoRoomAtAll);
    RUN_TEST(testFitTextAdversarialMetricStaysInBounds);
    RUN_TEST(testFitTextTightButPossible);
    RUN_TEST(testFitTextZeroSizeDestination);
    RUN_TEST(testFitTextEmptyInput);
    return UNITY_END();
}
