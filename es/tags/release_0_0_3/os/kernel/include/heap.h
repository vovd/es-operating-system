/*
 * Copyright (c) 2006
 * Nintendo Co., Ltd.
 *  
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Nintendo makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

#ifndef NINTENDO_ES_KERNEL_HEAP_H_INCLUDED
#define NINTENDO_ES_KERNEL_HEAP_H_INCLUDED

//
//   buckets[0]       Mass              Cell           Cell           Cell
//  +--------+       +--------+        +-+-+-+----+   +-+-+-+----+   +-+-+-+----+
//  |  head  |------>|  head  |------->|p|n|s|    |---|p|n|s|    |---|p|n|s|    |<-+
//  +--------+       +--------+        |r|e|i|    |   |r|e|i|    |   |r|e|i|    |  |
//  |  tail  |--+    |  tail  |---+    |e|x|z|    |   |e|x|z|    |   |e|x|z|    |  |
//  +--------+  |    +--------+   |    |v|t|e|    |   |v|t|e|    |   |v|t|e|    |  |
//  |  size  |<-|-+  |  used  |   |    +-+-+-+----+   +-+-+-+----+   +-+-+-+----+  |
//  +--------+  | |  +--------+   |                                                |
//              | +--|  bucket|   +------------------------------------------------+
//   buckets[1] |    +--------+
//  +--------+  |    |  prev  |--> 0
//  |  head  |  |    +--------+
//  +--------+  | +->|  next  |---+
//  |  tail  |  | |  +--------+   |
//  +--------+  | |               |
//  |  size  |  | |         +-----+
//  +--------+  | |         |
//              | |   Mass  V           Cell           Cell           Cell
//      :       |    +--------+        +-+-+-+----+   +-+-+-+----+   +-+-+-+----+
//      :       +--->|  head  |------->|p|n|s|    |---|p|n|s|    |---|p|n|s|    |<-+
//      :            +--------+        |r|e|i|    |   |r|e|i|    |   |r|e|i|    |  |
//                |  |  tail  |---+    |e|x|z|    |   |e|x|z|    |   |e|x|z|    |  |
//                |  +--------+   |    |v|t|e|    |   |v|t|e|    |   |v|t|e|    |  |
//                |  |  used  |   |    +-+-+-+----+   +-+-+-+----+   +-+-+-+----+  |
//                |  +--------+   |                                                |
//                |  |  bucket|   +------------------------------------------------+
//                |  +--------+
//                +--|  prev  |
//                   +--------+
//                   |  next  |--> 0
//                   +--------+
//
//   listLargeCell     Cell                    Cell
//  +--------+        +-+-+-+-------------+   +-+-+-+-------------+
//  |  head  |------->|p|n|s|             |---|p|n|s|             |<-+
//  +--------+        |r|e|i|             |   |r|e|i|             |  |
//  |  tail  |---+    |e|x|z|             |   |e|x|z|             |  |
//  +--------+   |    |v|t|e|             |   |v|t|e|             |  |
//               |    +-+-+-+-------------+   +-+-+-+-------------+  |
//               |                                                   |
//               +---------------------------------------------------+
//

#include <cstddef>
#include "arena.h"
#include "cache.h"
#include "thread.h"

class Heap
{
    SpinLock    spinLock;
    Arena&      arena;
    size_t      thresh;

public:
    Heap(Arena& arena);
    ~Heap();

    void* alloc(size_t size);
    void free(void* place);
    void* realloc(void *ptr, size_t size);

private:
    static const size_t BUCKET_SIZE = 9;

    struct Bucket;

    struct Cell
    {
        static const size_t SIZE;

        size_t      size;       // Cell size
        Link<Cell>  linkCell;

        typedef List<Cell, &Cell::linkCell> List;

        explicit Cell(size_t size) : size(size)
        {
        }

        void* getData()
        {
            return reinterpret_cast<char*>(this) + SIZE;
        }
    };

    struct Mass
    {
        static const size_t SIZE;

        int         used;       // Number of cells in use.
        Link<Mass>  linkMass;
        Bucket*     bucket;
        Cell::List  listCell;

        typedef List<Mass, &Mass::linkMass> List;

        explicit Mass(Bucket* bucket);
        Cell* getCell();
        size_t putCell(Cell* cell);
    };

    struct Bucket
    {
        SpinLock    spinLock;
        size_t      size;       // Cell size
        Arena*      arena;
        Mass::List  listMass;

        Bucket() : size(0), arena(0)
        {
        }

        void* allocMass()
        {
            return arena->allocLast(Page::SIZE, Page::SIZE);
        }

        void freeMass(void* place)
        {
            arena->free(place, Page::SIZE);
        }

        Cell* alloc(size_t size);
        size_t free(Mass* mass, Cell* cell);
    };

    // For 28, 60, 124, 252, 504, 1008, 2016 byte objects
    Bucket        buckets[BUCKET_SIZE];
    Cell::List    listLargeCell;

    bool isLargeCell(size_t size)
    {
        return (thresh < size) ? true : false;
    }

    Cell* getCell(void* place)
    {
        return reinterpret_cast<Cell*>(reinterpret_cast<char*>(place) - Cell::SIZE);
    }

    Mass* getMass(const Cell* cell)
    {
        ASSERT(!isLargeCell(cell->size));
        Mass* mass = reinterpret_cast<Mass*>(reinterpret_cast<size_t>(cell) & ~(Page::SIZE - 1));
        ASSERT(mass->bucket->arena == &arena);
        ASSERT(mass->bucket->size == cell->size);
        return mass;
    }

    Bucket* getBucket(size_t size);
};

#endif // NINTENDO_ES_KERNEL_HEAP_H_INCLUDED
