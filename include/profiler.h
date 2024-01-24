#pragma once

#define ENABLE_API_PROFILER 1

// Define API_PROFILER_XXX based on the platform
#if defined(_WIN32) || defined(_WIN64)
    #define API_PROFILER_WINDOWS
    #include <profileapi.h>			// QueryPerformanceCounter & QueryPerformanceFrequency
    #include <winnt.h>				// MemoryBarrier & LARGE_INTEGER
	#include <processthreadsapi.h>	// GetCurrentThreadId
#else
    #define API_PROFILER_UNIX
	#include <time.h>
#endif

#include <cstdio>
#include <pthread.h>

#if ENABLE_API_PROFILER

class APIProfiler {
public:
    struct ThreadInfo {
#ifdef API_PROFILER_WINDOWS
        INT64 lastReportTime;
        INT64 accumulator;
        INT64 hitCount;
#else
		timespec lastReportTime;
        long long accumulator;
        long long hitCount;
#endif
        const char *name;
    };

private:
    ThreadInfo *m_threadInfo;
#ifdef API_PROFILER_WINDOWS
	INT64 m_start;
    static float s_ooFrequency;
    static INT64 s_reportInterval;
    void Flush(INT64 end);
#else
    timespec m_start;
    static long long s_reportInterval;
    void Flush(timespec end);
#endif


public:
	inline APIProfiler(ThreadInfo *threadInfo)
	{
#ifdef API_PROFILER_WINDOWS
		LARGE_INTEGER start;
		QueryPerformanceCounter(&start);
		m_start = start.QuadPart;
		m_threadInfo = threadInfo;
#else
		clock_gettime(CLOCK_MONOTONIC, &m_start);
		m_threadInfo = threadInfo;
#endif
	}

	inline ~APIProfiler()
	{
#ifdef API_PROFILER_WINDOWS
		LARGE_INTEGER end;
		QueryPerformanceCounter(&end);
		m_threadInfo->accumulator += (end.QuadPart - m_start);
		m_threadInfo->hitCount++;
		if (end.QuadPart - m_threadInfo->lastReportTime > s_reportInterval)
			Flush(end.QuadPart);
#else
		timespec end;
		clock_gettime(CLOCK_MONOTONIC, &end);
		
		long long startNanoSec = m_start.tv_sec * 1e9 + m_start.tv_nsec;
		long long endNanoSec = end.tv_sec * 1e9 + end.tv_nsec;
		long long lastReportNanoSec = m_threadInfo->lastReportTime.tv_sec * 1e9 + m_threadInfo->lastReportTime.tv_nsec;
		
		m_threadInfo->accumulator += (endNanoSec - startNanoSec) / 1000LL;
		m_threadInfo->hitCount++;
		
		if ( (( endNanoSec - lastReportNanoSec ) / 1000LL) > s_reportInterval )
			Flush(end);
#endif
	}
};

#endif  // ENABLE_API_PROFILER


#if ENABLE_API_PROFILER

#define DECLARE_API_PROFILER(name) \
	extern thread_local APIProfiler::ThreadInfo __APIProfiler_##name;

#ifdef API_PROFILER_WINDOWS
#define DEFINE_API_PROFILER(name) \
    thread_local APIProfiler::ThreadInfo __APIProfiler_##name = { 0, 0, 0, #name };
#endif

#ifdef API_PROFILER_UNIX
#define DEFINE_API_PROFILER(name) \
    thread_local APIProfiler::ThreadInfo __APIProfiler_##name = { {0, 0}, 0, 0, #name };
#endif

#define TOKENPASTE2(x, y) x ## y
#define TOKENPASTE(x, y) TOKENPASTE2(x, y)

#define API_PROFILER(name) \
	APIProfiler TOKENPASTE(__APIProfiler_##name, __LINE__)(&__APIProfiler_##name)

#else // Macros evaluate to nothing when profiler disabled
	
#define DECLARE_API_PROFILER(name)
#define DEFINE_API_PROFILER(name)
#define API_PROFILER(name)

#endif  // ENABLE_API_PROFILER


#if ENABLE_API_PROFILER

#ifdef API_PROFILER_WINDOWS
static const float APIProfiler_ReportIntervalSecs = 1.0f;
float APIProfiler::s_ooFrequency = 0;
INT64 APIProfiler::s_reportInterval = 0;

void APIProfiler::Flush(INT64 end)
{
	// Initialize globals based on timer frequency if not already initialized
    if (s_reportInterval == 0)
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        s_ooFrequency = 1.0f / freq.QuadPart;
        MemoryBarrier();
        s_reportInterval = static_cast<INT64>(freq.QuadPart * APIProfiler_ReportIntervalSecs);
    }

	// Avoid garbage timing on the first call by initializing a new interval
    if (m_threadInfo->lastReportTime == 0)
    {
        m_threadInfo->lastReportTime = m_start;
        return;
    }

	// Calculate the time interval since the last report and the total measured time
    float interval = (end - m_threadInfo->lastReportTime) * s_ooFrequency;
    float measured = m_threadInfo->accumulator * s_ooFrequency;
	
    printf("TID 0x%x time spent in \"%s\": %.0f/%.0f microsec %.1f%% %dx\n",
        GetCurrentThreadId(),
        m_threadInfo->name,
        measured * 1000000,
        interval * 1000000,
        100.f * measured / interval,
        m_threadInfo->hitCount);

    m_threadInfo->lastReportTime = end;
    m_threadInfo->accumulator = 0;
    m_threadInfo->hitCount = 0;
}

#else	// API_PROFILER_UNIX defined

static const double APIProfiler_ReportIntervalSecs = 1.0f;
long long APIProfiler::s_reportInterval = 0;

void APIProfiler::Flush(timespec end)
{
    if (s_reportInterval == 0)
    {
        s_reportInterval = static_cast<long long>(1e6 * APIProfiler_ReportIntervalSecs);
    }

    if (m_threadInfo->lastReportTime.tv_sec == 0 && m_threadInfo->lastReportTime.tv_nsec == 0)
    {
        m_threadInfo->lastReportTime = m_start;
        return;
    }

    double interval = static_cast<double>((end.tv_sec - m_threadInfo->lastReportTime.tv_sec) * 1000000000LL +
                      (end.tv_nsec - m_threadInfo->lastReportTime.tv_nsec)) / 1000.0f;

    double measured = m_threadInfo->accumulator;

    printf("TID %ld time spent in \"%s\": %.0f/%.0f microsec %.1f%% %lldx\n",
           pthread_self(),
           m_threadInfo->name,
           measured,
           interval,
           100.0 * measured / interval,
           m_threadInfo->hitCount);

    m_threadInfo->lastReportTime = end;
    m_threadInfo->accumulator = 0;
    m_threadInfo->hitCount = 0;
}

#endif	//API_PROFILER_WINDOWS

#endif	//ENABLE_API_PROFILER