/* Host tests for gate_fusion: pairing, verdicts, singles, laps and
 * calibration. pio test -e native. GPL-3.0-or-later. */
#include <unity.h>
#include "../../src/gate_fusion.c"

static const fusion_cfg_t CFG = {
    .pair_window_ms = 75, .pair_wait_ms = 1000, .delta_max = 24, .offset = 0, .min_lap_ms = 5000};
static fusion_t f;
static fusion_event_t e;

void setUp(void) { fusion_init(&f, &CFG); }
void tearDown(void) {}

/* Both boards report a pass on ch, arriving 100 ms after their pass times. */
static void both(unsigned ch, uint32_t t_top, uint8_t top, uint32_t t_bot, uint8_t bot) {
    fusion_pass(&f, FUSION_TOP, ch, t_top, top, t_top + 100);
    fusion_pass(&f, FUSION_BOTTOM, ch, t_bot, bot, t_bot + 100);
}

static void test_centre_pass_is_valid(void) {
    both(0, 10000, 150, 10010, 146);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_VALID, e.verdict);
    TEST_ASSERT_TRUE(e.counted);
    TEST_ASSERT_EQUAL(4, e.delta);
    TEST_ASSERT_EQUAL(10005, e.t_ms);
    TEST_ASSERT_EQUAL(150, e.peak[FUSION_TOP]);
    TEST_ASSERT_EQUAL(146, e.peak[FUSION_BOTTOM]);
    TEST_ASSERT_FALSE(fusion_next(&f, &e));
}

static void test_top_heavy_is_over(void) {
    both(1, 10000, 180, 10000, 140); /* +10 dB */
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_OVER, e.verdict);
    TEST_ASSERT_FALSE(e.counted);
    TEST_ASSERT_EQUAL(40, e.delta);
}

static void test_bottom_heavy_is_under(void) {
    both(1, 10000, 130, 10000, 170);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_UNDER, e.verdict);
    TEST_ASSERT_EQUAL(-40, e.delta);
}

static void test_delta_limit_is_inclusive(void) {
    both(0, 10000, 164, 10000, 140); /* exactly +24 */
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_VALID, e.verdict);
    both(1, 10000, 140, 10000, 164);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_VALID, e.verdict);
}

static void test_one_board_only_is_single_after_wait(void) {
    fusion_pass(&f, FUSION_TOP, 2, 10000, 170, 10100);
    fusion_poll(&f, 11099);
    TEST_ASSERT_FALSE(fusion_next(&f, &e));
    fusion_poll(&f, 11100);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_SINGLE_TOP, e.verdict);
    TEST_ASSERT_EQUAL(170, e.peak[FUSION_TOP]);
    TEST_ASSERT_EQUAL(0, e.peak[FUSION_BOTTOM]);
    TEST_ASSERT_FALSE(e.counted);
}

static void test_pass_times_too_far_apart_do_not_pair(void) {
    fusion_pass(&f, FUSION_TOP, 0, 10000, 150, 10100);
    fusion_pass(&f, FUSION_BOTTOM, 0, 10100, 150, 10200); /* 100 ms apart */
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_SINGLE_TOP, e.verdict); /* older, can't pair any more */
    fusion_poll(&f, 11200);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_SINGLE_BOTTOM, e.verdict);
}

static void test_late_arrival_still_pairs(void) {
    /* The bottom board's crossing ends 600 ms after the top board's. */
    fusion_pass(&f, FUSION_TOP, 0, 10000, 150, 10100);
    fusion_pass(&f, FUSION_BOTTOM, 0, 10040, 152, 10700);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_VALID, e.verdict);
}

static void test_channels_are_independent(void) {
    fusion_pass(&f, FUSION_TOP, 0, 10000, 150, 10100);
    fusion_pass(&f, FUSION_BOTTOM, 3, 10000, 150, 10100);
    TEST_ASSERT_FALSE(fusion_next(&f, &e));
    fusion_poll(&f, 11100);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_FALSE(fusion_next(&f, &e));
}

static void test_laps_and_min_lap(void) {
    both(0, 10000, 150, 10000, 150);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_TRUE(e.counted);
    TEST_ASSERT_EQUAL(0, e.lap_ms);
    both(0, 13000, 150, 13000, 150); /* 3 s: under the 5 s minimum */
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_VALID, e.verdict);
    TEST_ASSERT_FALSE(e.counted);
    both(0, 22500, 150, 22500, 150);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_TRUE(e.counted);
    TEST_ASSERT_EQUAL(12500, e.lap_ms);
}

static void test_rejected_pass_does_not_start_lap(void) {
    both(0, 10000, 190, 10000, 140); /* OVER */
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    both(0, 12000, 150, 12000, 150); /* first counted lap */
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_TRUE(e.counted);
    TEST_ASSERT_EQUAL(0, e.lap_ms);
}

static void test_calibration_sets_offset(void) {
    /* The top board reads 3 dB hotter: a centre pass looks like +12. */
    fusion_calibrate(&f, 3);
    TEST_ASSERT_TRUE(fusion_calibrating(&f));
    both(0, 10000, 162, 10000, 150);
    both(1, 10000, 160, 10000, 150);
    both(2, 10000, 165, 10000, 150);
    TEST_ASSERT_FALSE(fusion_calibrating(&f));
    TEST_ASSERT_EQUAL(12, f.cfg.offset); /* (12 + 10 + 15) / 3 = 12.3 */
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_TRUE(fusion_next(&f, &e));
        TEST_ASSERT_TRUE(e.calibration);
    }
    /* +40 raw is now +28: still OVER; +30 raw is +18: VALID. */
    both(3, 20000, 190, 20000, 150);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_OVER, e.verdict);
    TEST_ASSERT_FALSE(e.calibration);
    both(3, 30000, 180, 30000, 150);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_VALID, e.verdict);
    TEST_ASSERT_EQUAL(18, e.delta);
}

static void test_negative_calibration_rounds(void) {
    fusion_calibrate(&f, 2);
    both(0, 10000, 150, 10000, 155);
    both(0, 20000, 150, 20000, 156); /* -5.5 → -6 */
    TEST_ASSERT_EQUAL(-6, f.cfg.offset);
}

static void test_clock_wrap(void) {
    uint32_t t = 0xFFFFFFF0u;
    fusion_pass(&f, FUSION_TOP, 0, t, 150, t + 5);
    fusion_pass(&f, FUSION_BOTTOM, 0, t + 40, 150, t + 60); /* wraps */
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_EQUAL(FUSION_VALID, e.verdict);
    TEST_ASSERT_EQUAL_UINT32(t + 20, e.t_ms);
}

static void test_clear_forgets_laps(void) {
    both(0, 10000, 150, 10000, 150);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    fusion_clear(&f);
    both(0, 11000, 150, 11000, 150);
    TEST_ASSERT_TRUE(fusion_next(&f, &e));
    TEST_ASSERT_TRUE(e.counted);
    TEST_ASSERT_EQUAL(0, e.lap_ms);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_centre_pass_is_valid);
    RUN_TEST(test_top_heavy_is_over);
    RUN_TEST(test_bottom_heavy_is_under);
    RUN_TEST(test_delta_limit_is_inclusive);
    RUN_TEST(test_one_board_only_is_single_after_wait);
    RUN_TEST(test_pass_times_too_far_apart_do_not_pair);
    RUN_TEST(test_late_arrival_still_pairs);
    RUN_TEST(test_channels_are_independent);
    RUN_TEST(test_laps_and_min_lap);
    RUN_TEST(test_rejected_pass_does_not_start_lap);
    RUN_TEST(test_calibration_sets_offset);
    RUN_TEST(test_negative_calibration_rounds);
    RUN_TEST(test_clock_wrap);
    RUN_TEST(test_clear_forgets_laps);
    return UNITY_END();
}
