#include "../src/live_index.h"
#include <cassert>
#include <cstdio>
#include <vector>

using namespace hp3live;

// Fake "object" addresses. They only have to be distinct and stable.
static char pool[4096];
static void *obj(int i) { return &pool[i * 4]; }

int main()
{
    // 1. Membership is exact for everything that was in the table.
    {
        Index ix;
        const void *table[64];
        for (int i = 0; i < 64; ++i) table[i] = obj(i);
        assert(ix.build(table, 64, 1000));
        assert(ix.exact);
        for (int i = 0; i < 64; ++i) assert(ix.contains(obj(i)));
    }

    // 2. A pointer that was never in the table is reported absent. This is the
    //    property the v57 crash fix depends on: a freed actor must not be
    //    handed to GetFullName.
    {
        Index ix;
        const void *table[32];
        for (int i = 0; i < 32; ++i) table[i] = obj(i);
        assert(ix.build(table, 32, 1000));
        for (int i = 32; i < 64; ++i) assert(!ix.contains(obj(i)));
        assert(!ix.contains(0));
    }

    // 3. NULL slots in the table are skipped without poisoning the probe.
    {
        Index ix;
        const void *table[16];
        for (int i = 0; i < 16; ++i) table[i] = (i % 2) ? 0 : obj(i);
        assert(ix.build(table, 16, 1000));
        for (int i = 0; i < 16; i += 2) assert(ix.contains(obj(i)));
        for (int i = 1; i < 16; i += 2) assert(!ix.contains(obj(i)));
    }

    // 4. Duplicate entries are idempotent.
    {
        Index ix;
        const void *table[8] = { obj(0), obj(1), obj(0), obj(1),
                                 obj(2), obj(0), obj(2), obj(1) };
        assert(ix.build(table, 8, 1000));
        assert(ix.contains(obj(0)) && ix.contains(obj(1)) && ix.contains(obj(2)));
    }

    // 5. An unusable table leaves the index non-exact so callers fall back to
    //    their own linear check instead of trusting an empty snapshot.
    {
        Index ix;
        assert(!ix.build(0, 10, 1000));
        assert(!ix.exact);
        assert(!ix.contains(obj(0)));
        const void *table[4] = { obj(0), obj(1), obj(2), obj(3) };
        assert(!ix.build(table, 0, 1000));
        assert(!ix.exact);
    }

    // 6. Freshness: same table, same length, inside the window -> reusable.
    {
        Index ix;
        const void *table[8];
        for (int i = 0; i < 8; ++i) table[i] = obj(i);
        assert(ix.build(table, 8, 1000));
        assert(ix.fresh(table, 8, 1000, 16));
        assert(ix.fresh(table, 8, 1015, 16));
        assert(!ix.fresh(table, 8, 1016, 16));      // aged out
    }

    // 7. A length change means objects were created or destroyed, so the
    //    snapshot is stale even inside the refresh window.
    {
        Index ix;
        const void *table[8];
        for (int i = 0; i < 8; ++i) table[i] = obj(i);
        assert(ix.build(table, 8, 1000));
        assert(!ix.fresh(table, 9, 1001, 16));
        assert(!ix.fresh(table, 7, 1001, 16));
    }

    // 8. A different table pointer is never considered fresh.
    {
        Index ix;
        const void *a[4] = { obj(0), obj(1), obj(2), obj(3) };
        const void *b[4] = { obj(4), obj(5), obj(6), obj(7) };
        assert(ix.build(a, 4, 1000));
        assert(ix.fresh(a, 4, 1001, 16));
        assert(!ix.fresh(b, 4, 1001, 16));
    }

    // 9. GetTickCount wraparound: a snapshot taken just before the rollover is
    //    still fresh just after it, and ages out correctly.
    {
        Index ix;
        const void *table[4] = { obj(0), obj(1), obj(2), obj(3) };
        uint32_t t = 0xFFFFFFF8u;
        assert(ix.build(table, 4, t));
        assert(ix.fresh(table, 4, (uint32_t)(t + 5), 16));
        assert(!ix.fresh(table, 4, (uint32_t)(t + 16), 16));
    }

    // 10. Scale: a table the size of HP3_InsideHub's GObjObjects (8820
    //     entries, per the field log) stays exact and answers correctly.
    {
        Index ix;
        std::vector<const void *> table(8820);
        for (int i = 0; i < 8820; ++i) table[i] = (const void *)(size_t)(0x10000u + i * 64u);
        assert(ix.build(&table[0], 8820, 5000));
        assert(ix.exact);
        for (int i = 0; i < 8820; ++i) assert(ix.contains(table[i]));
        // Just-past-the-end and just-before-the-start pointers are absent.
        assert(!ix.contains((const void *)(size_t)(0x10000u + 8820u * 64u)));
        assert(!ix.contains((const void *)(size_t)(0x10000u - 64u)));
    }

    // 11. Over the load cap the index reports itself inexact rather than
    //     silently answering from a partial snapshot.
    {
        Index ix;
        std::vector<const void *> table(kMaxEntries + 1);
        for (int i = 0; i < kMaxEntries + 1; ++i)
            table[i] = (const void *)(size_t)(0x20000u + i * 64u);
        assert(ix.build(&table[0], kMaxEntries + 1, 5000));
        assert(!ix.exact);
        assert(!ix.contains(table[0]));      // must not be trusted
        assert(!ix.fresh(&table[0], kMaxEntries + 1, 5000, 16));
    }

    // 12. reset()/clear() drop membership.
    {
        Index ix;
        const void *table[4] = { obj(0), obj(1), obj(2), obj(3) };
        assert(ix.build(table, 4, 1000));
        assert(ix.contains(obj(0)));
        ix.clear();
        assert(!ix.contains(obj(0)));
        assert(!ix.exact);
        assert(ix.build(table, 4, 2000));
        assert(ix.contains(obj(3)));
        ix.reset();
        assert(!ix.contains(obj(3)) && ix.count == 0 && ix.table == 0);
    }

    // 13. Slot mixing actually spreads clustered, pool-aligned pointers
    //     (UObject allocations are 16-byte aligned, so the low bits are
    //     useless on their own).
    {
        unsigned seen[8] = { 0 };
        for (int i = 0; i < 4096; ++i)
            ++seen[slotFor(obj(i)) & 7u];
        for (int b = 0; b < 8; ++b) assert(seen[b] > 0);
    }

    std::puts("live index: exact membership, staleness, scale and load-cap "
              "assertions passed");
    return 0;
}
