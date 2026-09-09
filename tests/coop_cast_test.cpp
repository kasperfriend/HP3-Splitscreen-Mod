#include "../src/coop_cast.h"
#include <cassert>
#include <cstdio>

int main()
{
    int objectA = 0, objectB = 0, classA = 0, classB = 0;
    hp3coop::Target a = { &objectA, &classA, 17 };
    hp3coop::Target b = { &objectB, &classA, 18 };
    hp3coop::Target recycled = { &objectA, &classB, 17 };
    hp3coop::Target invalid = { 0, &classA, 17 };
    hp3coop::Hold hold = {};

    assert(hp3coop::update(hold, true, a, 100, hp3coop::DefaultHoldMs)
           == hp3coop::Started);
    assert(hp3coop::update(hold, true, a, 10099, hp3coop::DefaultHoldMs)
           == hp3coop::Waiting);
    // Exact 10 seconds is still a normal hold; the fallback crosses only
    // after 10 seconds have elapsed.
    assert(hp3coop::update(hold, true, a, 10100, hp3coop::DefaultHoldMs)
           == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, a, 10101, hp3coop::DefaultHoldMs)
           == hp3coop::Ready);
    assert(hp3coop::update(hold, true, a, 30101, hp3coop::DefaultHoldMs)
           == hp3coop::AlreadyReady);

    // Releasing or moving the cursor off the object cannot carry timer credit
    // to a later hold.
    assert(hp3coop::update(hold, false, a, 30200, hp3coop::DefaultHoldMs)
           == hp3coop::Reset);
    assert(!hold.active && !hold.fired);
    assert(hp3coop::update(hold, true, a, 40000, hp3coop::DefaultHoldMs)
           == hp3coop::Started);
    assert(hp3coop::update(hold, true, b, 49999, hp3coop::DefaultHoldMs)
           == hp3coop::Started);
    assert(hp3coop::update(hold, true, b, 59999, hp3coop::DefaultHoldMs)
           == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, b, 60000, hp3coop::DefaultHoldMs)
           == hp3coop::Ready);

    // A recycled object-table slot has a different class identity and must
    // restart instead of completing against a stale object pointer.
    assert(hp3coop::update(hold, true, recycled, 60000,
                            hp3coop::DefaultHoldMs) == hp3coop::Started);
    assert(hp3coop::update(hold, true, invalid, 60001,
                            hp3coop::DefaultHoldMs) == hp3coop::Reset);

    // GetTickCount is a 32-bit clock. The policy stays correct across wrap.
    hp3coop::clear(hold);
    assert(hp3coop::update(hold, true, a, 0xFFFFFF00u,
                            hp3coop::DefaultHoldMs) == hp3coop::Started);
    assert(hp3coop::update(hold, true, a, 9743u,
                            hp3coop::DefaultHoldMs) == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, a, 9744u,
                            hp3coop::DefaultHoldMs) == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, a, 9745u,
                            hp3coop::DefaultHoldMs) == hp3coop::Ready);

    // v57 grace: a target that flickers away for <= graceMs keeps the dwell
    // running against the last valid sample; a longer gap resets it. The
    // pre-fire dwell only ever accumulates against ONE uninterrupted hold.
    hp3coop::clear(hold);
    const std::uint32_t grace = 250u;
    assert(hp3coop::update(hold, true, a, 100000,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Started);
    assert(hp3coop::update(hold, true, a, 105000,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Waiting);
    // one-frame loss inside grace: dwell continues
    assert(hp3coop::update(hold, false, invalid, 105200,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, a, 105300,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Waiting);
    // 251ms loss: real release, full reset
    assert(hp3coop::update(hold, false, invalid, 108000,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Reset);
    assert(!hold.active && !hold.fired);
    // the grace must not carry credit across the reset
    assert(hp3coop::update(hold, true, a, 108001,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Started);
    assert(hp3coop::update(hold, true, a, 108001 + 9999,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Waiting);
    // flicker right at the boundary then past 10s total: fires while held
    assert(hp3coop::update(hold, false, invalid, 108001 + 10050,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, a, 108001 + 10100,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Ready);
    // after Ready the grace no longer applies: a loss is a loss
    assert(hp3coop::update(hold, false, invalid, 108001 + 10400,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Reset);

    // graceMs == 0 keeps the original hard reset on a single invalid sample.
    hp3coop::clear(hold);
    assert(hp3coop::update(hold, true, a, 200000,
                            hp3coop::DefaultHoldMs, 0) == hp3coop::Started);
    assert(hp3coop::update(hold, false, invalid, 200001,
                            hp3coop::DefaultHoldMs, 0) == hp3coop::Reset);

    // grace is capped by the hold still being unfired: a *different* valid
    // target always starts a fresh interval, never inherits the dwell.
    hp3coop::clear(hold);
    assert(hp3coop::update(hold, true, a, 300000,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Started);
    assert(hp3coop::update(hold, true, b, 300100,
                            hp3coop::DefaultHoldMs, grace) == hp3coop::Started);

    puts("co-op cast hold policy: continuous target, reset, identity, wrap and "
         "flicker-grace assertions passed");
}
