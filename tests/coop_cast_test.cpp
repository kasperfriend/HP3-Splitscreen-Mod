#include "../src/coop_cast.h"
#include <cassert>
#include <cstdio>

int main()
{
    const std::uint32_t H = hp3coop::DefaultHoldMs; // 8000 ms
    int objectA = 0, objectB = 0, classA = 0, classB = 0;
    hp3coop::Target a = { &objectA, &classA, 17 };
    hp3coop::Target b = { &objectB, &classA, 18 };
    hp3coop::Target recycled = { &objectA, &classB, 17 };
    hp3coop::Target invalid = { 0, &classA, 17 };
    hp3coop::Hold hold = {};

    assert(hp3coop::update(hold, true, a, 100, H)
           == hp3coop::Started);
    assert(hp3coop::update(hold, true, a, 100 + H - 1, H)
           == hp3coop::Waiting);
    // Exactly the configured interval is still a normal hold; the fallback
    // crosses only after the interval has fully elapsed.
    assert(hp3coop::update(hold, true, a, 100 + H, H)
           == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, a, 100 + H + 1, H)
           == hp3coop::Ready);
    assert(hp3coop::update(hold, true, a, 100 + 3 * H + 1, H)
           == hp3coop::AlreadyReady);

    // Releasing or moving the cursor off the object cannot carry timer credit
    // to a later hold.
    assert(hp3coop::update(hold, false, a, 100 + 3 * H + 2, H)
           == hp3coop::Reset);
    assert(!hold.active && !hold.fired);
    assert(hp3coop::update(hold, true, a, 40000, H)
           == hp3coop::Started);
    assert(hp3coop::update(hold, true, b, 49999, H)
           == hp3coop::Started);
    assert(hp3coop::update(hold, true, b, 49999 + H, H)
           == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, b, 49999 + H + 1, H)
           == hp3coop::Ready);

    // A recycled object-table slot has a different class identity and must
    // restart instead of completing against a stale object pointer.
    assert(hp3coop::update(hold, true, recycled, 60000, H) == hp3coop::Started);
    assert(hp3coop::update(hold, true, invalid, 60001, H) == hp3coop::Reset);

    // GetTickCount is a 32-bit clock. The policy stays correct across wrap.
    hp3coop::clear(hold);
    assert(hp3coop::update(hold, true, a, 0xFFFFFF00u, H) == hp3coop::Started);
    assert(hp3coop::update(hold, true, a, (std::uint32_t)(0xFFFFFF00u + H - 1),
                            H) == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, a, (std::uint32_t)(0xFFFFFF00u + H),
                            H) == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, a, (std::uint32_t)(0xFFFFFF00u + H + 1),
                            H) == hp3coop::Ready);

    // v57 grace: a target that flickers away for <= graceMs keeps the dwell
    // running against the last valid sample; a longer gap resets it. The
    // pre-fire dwell only ever accumulates against ONE uninterrupted hold.
    hp3coop::clear(hold);
    const std::uint32_t grace = 250u;
    assert(hp3coop::update(hold, true, a, 100000, H, grace) == hp3coop::Started);
    assert(hp3coop::update(hold, true, a, 105000, H, grace) == hp3coop::Waiting);
    // one-frame loss inside grace: dwell continues
    assert(hp3coop::update(hold, false, invalid, 105200, H, grace)
           == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, a, 105300, H, grace) == hp3coop::Waiting);
    // 251ms loss: real release, full reset
    assert(hp3coop::update(hold, false, invalid, 108000, H, grace)
           == hp3coop::Reset);
    assert(!hold.active && !hold.fired);
    // the grace must not carry credit across the reset
    assert(hp3coop::update(hold, true, a, 108001, H, grace) == hp3coop::Started);
    assert(hp3coop::update(hold, true, a, 108001 + H - 1, H, grace)
           == hp3coop::Waiting);
    // flicker right at the boundary then past the interval: fires while held
    assert(hp3coop::update(hold, false, invalid, 108001 + H + 50, H, grace)
           == hp3coop::Waiting);
    assert(hp3coop::update(hold, true, a, 108001 + H + 100, H, grace)
           == hp3coop::Ready);
    // after Ready the grace no longer applies: a loss is a loss
    assert(hp3coop::update(hold, false, invalid, 108001 + H + 400, H, grace)
           == hp3coop::Reset);

    // graceMs == 0 keeps the original hard reset on a single invalid sample.
    hp3coop::clear(hold);
    assert(hp3coop::update(hold, true, a, 200000, H, 0) == hp3coop::Started);
    assert(hp3coop::update(hold, false, invalid, 200001, H, 0)
           == hp3coop::Reset);

    // grace is capped by the hold still being unfired: a *different* valid
    // target always starts a fresh interval, never inherits the dwell.
    hp3coop::clear(hold);
    assert(hp3coop::update(hold, true, a, 300000, H, grace) == hp3coop::Started);
    assert(hp3coop::update(hold, true, b, 300100, H, grace) == hp3coop::Started);

    puts("co-op cast hold policy: continuous target, reset, identity, wrap and "
         "flicker-grace assertions passed");
}
