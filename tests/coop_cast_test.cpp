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

    puts("co-op cast hold policy: continuous target, reset, identity and wrap assertions passed");
}
