#ifndef HP3_LIVE_INDEX_H
#define HP3_LIVE_INDEX_H

// O(1) liveness for cached UObject pointers.
//
// GObjObjects is the engine's own record of live UObjects, so table membership
// is the exact "is this object still alive" answer (see the v57 crash notes in
// dllmain.cpp). The v57 implementation answered that question with a linear
// walk of the whole table plus an IsBadReadPtr over all of it, on EVERY call.
// v66.1 left that in place while adding cached Lumos trigger/wall lists that
// are validated per actor, so a split-ON frame in HP3_InsideHub (8820 objects)
// paid dozens of full-table walks. This header keeps the same exact semantics
// but answers from a hash snapshot that is rebuilt at most once per
// refreshMs, so the per-frame cost is one build plus O(1) lookups.
//
// Pure policy: no engine types, so it is host-testable.

#include <stdint.h>
#include <stddef.h>

namespace hp3live {

// Slots must be a power of two. Load factor is capped at 50% so the probe
// stays short; a table too big for that degrades to inexact instead of
// silently slowing down.
enum { kSlots = 1 << 15, kMask = kSlots - 1, kMaxEntries = kSlots / 2 };

// UObject pointers are pool-allocated and therefore clustered; mixing the
// unused low bits keeps the probe chains short.
inline unsigned slotFor(const void *p)
{
    uint32_t v = (uint32_t)(size_t)p;
    v ^= v >> 15;
    v *= 0x9E3779B1u;
    v ^= v >> 13;
    return (unsigned)(v & (uint32_t)kMask);
}

struct Index {
    const void *slot[kSlots];
    const void *const *table;   // the array this snapshot was built from
    int count;                  // table length at build time
    uint32_t builtAt;
    bool exact;                 // every table entry made it into the hash

    Index() { reset(); }

    void reset()
    {
        for (int i = 0; i < kSlots; ++i) slot[i] = 0;
        table = 0;
        count = 0;
        builtAt = 0;
        exact = false;
    }

    void clear()
    {
        for (int i = 0; i < kSlots; ++i) slot[i] = 0;
        exact = false;
    }

    // Snapshot `n` entries of `data`. Returns false (and leaves the index
    // unusable) when the table cannot be read; the caller must then fall back
    // to its own linear check.
    bool build(const void *const *data, int n, uint32_t now)
    {
        reset();
        if (!data || n <= 0) return false;
        table = data;
        count = n;
        builtAt = now;
        exact = (n <= kMaxEntries);
        if (!exact) return true;   // membership still answered, but not trusted
        int placed = 0;
        for (int i = 0; i < n; ++i) {
            const void *o = data[i];
            if (!o) continue;
            unsigned s = slotFor(o);
            int guard = 0;
            while (slot[s] && slot[s] != o) {
                s = (s + 1) & (unsigned)kMask;
                if (++guard >= kSlots) { exact = false; break; }
            }
            if (!slot[s]) { slot[s] = o; ++placed; }
        }
        if (placed == 0 && n > 0) exact = false;
        return true;
    }

    // Definite membership. Only meaningful when `exact` is true; an inexact
    // index may hold a partial snapshot and a false here proves nothing.
    bool contains(const void *p) const
    {
        if (!p || !exact) return false;
        unsigned s = slotFor(p);
        for (int guard = 0; guard < kSlots; ++guard) {
            const void *e = slot[s];
            if (!e) return false;          // end of this probe chain
            if (e == p) return true;
            s = (s + 1) & (unsigned)kMask;
        }
        return false;
    }

    // A snapshot is reusable while it describes the same table, that table has
    // not changed length, and it is younger than maxAgeMs. A length change
    // means objects were created or destroyed, so the snapshot is stale even
    // inside the refresh window.
    bool fresh(const void *const *data, int n, uint32_t now,
               uint32_t maxAgeMs) const
    {
        if (!exact || !data || data != table || n != count) return false;
        return (uint32_t)(now - builtAt) < maxAgeMs;   // wrap-safe
    }
};

} // namespace hp3live

#endif // HP3_LIVE_INDEX_H
