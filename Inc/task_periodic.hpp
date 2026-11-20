/*
 * task_process.hpp
 *
 *  Created on: Nov 20, 2026
 *      Author: mgi
 */

#pragma once

#include "Logger.hpp"

template <typename LEDPin>
struct LedBlinker
{
    LEDPin pin{};
    bool toggle() { pin.toggle(); return true; }
};

struct HeapMonitor
{
    O1HeapInstance* heap;
    bool check()
    {
    	auto health = o1heapDoInvariantsHold(heap);
    	auto diagnostic = o1heapGetDiagnostics(heap);
    	log(LOG_LEVEL_INFO, "Memory: %d, (%4d %4d %4d)\r\n", health, diagnostic.capacity, diagnostic.peak_allocated, diagnostic.allocated);
    	return true;
    }
};
