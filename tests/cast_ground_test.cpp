#include "../src/cast_ground.h"
#include <assert.h>
#include <stdio.h>
#include <limits>

using namespace CastGround;

static int floorActor, platformActor;

static Sample standing(float z = 874.75f)
{
    Sample s = {};
    s.valid = true;
    s.physics = 0; // the mod's idle mode, not a measured floor height
    s.location[0] = -561.0f;
    s.location[1] = -6408.0f;
    s.location[2] = z;
    s.base = &floorActor;
    s.floor[2] = 1.0f;
    return s;
}

// Model the logged state exit: the pawn has not moved, but contact is gone.
static void stateExit(Sample &s)
{
    s.physics = 2;
    s.base = 0;
    s.floor[0] = s.floor[1] = s.floor[2] = 0.0f;
}

// A successful native collision probe supplies support, NOT a saved Z.
static void foundFloor(Sample &s)
{
    s.physics = 1;
    s.base = &floorActor;
    s.floor[2] = 1.0f;
    s.vz = 0.0f;
}

static void test_idle_to_delayed_walk()
{
    Sample s = standing();
    Recovery r = {};
    arm(r, s, 1000);
    stateExit(s);
    assert(step(r, s, 1000) == Reacquire);
    // Reentrant calls cannot issue the native twice.
    assert(step(r, s, 1000) == None);
    foundFloor(s);
    assert(repaired(r, s));
    assert(step(r, s, 1016) == KeepWalking);
    assert(step(r, s, 1032) == KeepWalking);
    assert(step(r, s, 1064) == Settled);
    assert(r.phase == Complete);
    // Idle may freeze ONLY now. Starting to walk after the old 900ms guard,
    // 1.5s swallow and even the 4s cast watch has expired needs no new repair.
    s.physics = 0;
    assert(step(r, s, 6000) == None);
    s.physics = 1;
    assert(supported(s));
    assert(step(r, s, 7000) == None);
    assert(s.location[2] == 874.75f);
}

static void test_no_support_is_not_a_retry_loop()
{
    Sample s = standing();
    Recovery r = {};
    arm(r, s, 1000);
    stateExit(s);
    assert(step(r, s, 1016) == Reacquire);
    // Native was called, but there is no floor (e.g. the edge of a platform).
    s.physics = 1;
    assert(!repaired(r, s));
    assert(r.phase == Released && r.reason == NoSupport);
    s.physics = 2; // adapter gives gravity back to the engine
    for (uint32_t t = 1032; t < 6000; t += 16)
        assert(step(r, s, t) == None);
    assert(s.location[2] == 874.75f);
}

static void test_recurring_fall_releases()
{
    Sample s = standing();
    Recovery r = {};
    arm(r, s, 1000);
    stateExit(s);
    assert(step(r, s, 1000) == Reacquire);
    foundFloor(s);
    assert(repaired(r, s));
    stateExit(s); // the next physics tick falls again, as in v48's log
    assert(step(r, s, 1016) == GiveUp);
    assert(r.reason == NoSupport);
    for (uint32_t t = 1032; t < 2000; t += 16)
        assert(step(r, s, t) == None);
}

static void test_walk_during_cast_uses_exit_position()
{
    Sample s = standing();
    // The player moved well beyond v48's 60/80-unit fire-position radius.
    s.location[0] += 250.0f;
    s.location[2] += 40.0f;
    Recovery r = {};
    arm(r, s, 1000);
    assert(r.exitLocation[0] == s.location[0]);
    assert(r.exitLocation[2] == s.location[2]);
    stateExit(s);
    s.location[0] += 3.0f;
    assert(step(r, s, 1016) == Reacquire);
    float z = s.location[2];
    foundFloor(s);
    assert(repaired(r, s));
    assert(step(r, s, 1048) == KeepWalking);
    assert(step(r, s, 1080) == Settled);
    assert(s.location[2] == z);
}

static void test_jump_vetoes()
{
    for (int mode = 0; mode < 3; ++mode) {
        Sample s = standing();
        Recovery r = {};
        arm(r, s, 1000);
        stateExit(s);
        if (mode == 0) s.jumping = true; // fresh key/pad, or standing-jump prep
        if (mode == 1) s.vz = 420.0f;   // released button, still ascending
        if (mode == 2) s.vz = -420.0f;  // real descending fall
        assert(step(r, s, 1016) == GiveUp);
        assert(r.reason == AirOrJump);
    }
    Sample s = standing();
    Recovery r = {};
    s.physics = 2;
    arm(r, s, 1000); // airborne before the state exit: no repair at all
    assert(r.phase == Released && r.reason == AirOrJump);
    assert(step(r, s, 1016) == None);
}

static void test_jump_during_verification()
{
    Sample s = standing();
    Recovery r = {};
    arm(r, s, 1000);
    stateExit(s);
    assert(step(r, s, 1000) == Reacquire);
    foundFloor(s);
    assert(repaired(r, s));
    s.jumping = true;
    assert(step(r, s, 1016) == GiveUp);
    assert(r.reason == AirOrJump);
}

static void test_jump_from_native_callback()
{
    for (int direction = -1; direction <= 1; direction += 2) {
        Sample s = standing();
        Recovery r = {};
        arm(r, s, 1000);
        stateExit(s);
        assert(step(r, s, 1000) == Reacquire);
        foundFloor(s);
        s.vz = direction * 420.0f; // a BaseChange script applied an impulse
        assert(!repaired(r, s));
        assert(r.reason == AirOrJump);
        assert(s.vz == direction * 420.0f);
    }
}

static void test_stale_or_moved_handoff()
{
    for (int mode = 0; mode < 4; ++mode) {
        Sample s = standing();
        Recovery r = {};
        arm(r, s, 1000);
        stateExit(s);
        if (mode == 0) s.location[0] += 1000.0f;
        if (mode == 1) s.location[2] -= 25.0f;
        if (mode == 2) s.location[2] += 10.0f;
        uint32_t now = mode == 3 ? 2000 : 1016;
        assert(step(r, s, now) == GiveUp);
        assert(r.reason == (mode == 3 ? Timeout : Moved));
    }
}

static void test_real_support_required()
{
    Sample s = standing();
    assert(supported(s));
    s.base = 0;
    assert(!supported(s));
    s.base = &floorActor;
    s.floor[2] = 0.0f;
    assert(!supported(s));
    s.floor[0] = 0.8f; s.floor[2] = 0.6f; // too steep
    assert(!supported(s));
    s.floor[0] = 0.6f; s.floor[2] = 0.8f; // walkable slope
    assert(supported(s));
    s.floor[2] = 10.0f; // invalid/non-unit cached normal
    assert(!supported(s));
    s.floor[2] = std::numeric_limits<float>::quiet_NaN();
    assert(!supported(s));
    s = standing();
    s.valid = false;
    assert(!supported(s));
}

static void test_support_can_change_on_platforms()
{
    Sample s = standing();
    Recovery r = {};
    arm(r, s, 1000);
    stateExit(s);
    assert(step(r, s, 1000) == Reacquire);
    foundFloor(s);
    s.base = &platformActor;
    s.floor[0] = 0.6f; s.floor[2] = 0.8f;
    assert(repaired(r, s));
    assert(step(r, s, 1032) == KeepWalking);
    s.base = &floorActor; // engine moved onto a different supported surface
    assert(step(r, s, 1064) == Settled);
}

static void test_zero_height_and_clock_wrap()
{
    Sample s = standing(0.0f); // Z=0 is valid, not an "uninitialized" sentinel
    Recovery r = {};
    uint32_t now = 0xffffffe0u;
    arm(r, s, now);
    stateExit(s);
    assert(step(r, s, now) == Reacquire);
    foundFloor(s);
    assert(repaired(r, s));
    assert(step(r, s, uint32_t(now + 16)) == KeepWalking);
    assert(step(r, s, uint32_t(now + 64)) == Settled);
    assert(s.location[2] == 0.0f);
}

static void test_reset_per_cast_and_player()
{
    Sample s = standing();
    Recovery players[3] = {};
    arm(players[1], s, 1000);
    stateExit(s);
    assert(step(players[1], s, 1000) == Reacquire);
    assert(players[2].phase == Inactive);
    foundFloor(s);
    assert(repaired(players[1], s));
    // New aim/pawn/level discards the old run even when the logger is off.
    Recovery fresh = {};
    players[1] = fresh;
    assert(step(players[1], s, 1016) == None);
    s.physics = 0;
    arm(players[1], s, 1020);
    stateExit(s);
    assert(step(players[1], s, 1020) == Reacquire);
    assert(players[1].walkingFrames == 0);
    assert(players[2].phase == Inactive);
}

static void test_missing_fields_or_native()
{
    Sample s = standing();
    Recovery r = {};
    s.valid = false;
    arm(r, s, 1000);
    assert(r.reason == InvalidSample);
    assert(step(r, s, 1016) == None);
    s = standing();
    arm(r, s, 1000);
    release(r, Unavailable);
    assert(step(r, s, 1016) == None);
    assert(s.physics == 0 && s.location[2] == 874.75f);
    // NoDip=0 never arms the policy.
    Recovery disabled = {};
    assert(step(disabled, s, 2000) == None);
}

static void test_unreadable_or_lost_contact_after_probe()
{
    for (int mode = 0; mode < 3; ++mode) {
        Sample s = standing();
        Recovery r = {};
        arm(r, s, 1000);
        stateExit(s);
        assert(step(r, s, 1000) == Reacquire);
        foundFloor(s);
        assert(repaired(r, s));
        if (mode == 0) s.base = 0;
        if (mode == 1) s.valid = false;
        if (mode == 2) s.location[2] = std::numeric_limits<float>::infinity();
        assert(step(r, s, 1016) == GiveUp);
        assert(r.reason == (mode == 0 ? NoSupport : InvalidSample));
        assert(step(r, s, 1032) == None);
    }
}

int main()
{
    test_idle_to_delayed_walk();
    test_no_support_is_not_a_retry_loop();
    test_recurring_fall_releases();
    test_walk_during_cast_uses_exit_position();
    test_jump_vetoes();
    test_jump_during_verification();
    test_jump_from_native_callback();
    test_stale_or_moved_handoff();
    test_real_support_required();
    test_support_can_change_on_platforms();
    test_zero_height_and_clock_wrap();
    test_reset_per_cast_and_player();
    test_missing_fields_or_native();
    test_unreadable_or_lost_contact_after_probe();
    puts("cast_ground: 14 regression cases passed");
    return 0;
}
