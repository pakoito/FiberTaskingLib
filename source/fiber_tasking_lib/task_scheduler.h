/* FiberTaskingLib - A tasking library that uses fibers for efficient task switching
 *
 * This library was created as a proof of concept of the ideas presented by
 * Christian Gyrling in his 2015 GDC Talk 'Parallelizing the Naughty Dog Engine Using Fibers'
 *
 * http://gdcvault.com/play/1022186/Parallelizing-the-Naughty-Dog-Engine
 *
 * FiberTaskingLib is the legal property of Adrian Astley
 * Copyright Adrian Astley 2015
 */

#pragma once

#include "fiber_tasking_lib/typedefs.h"

#include "concurrentqueue/blockingconcurrentqueue.h"

#include <atomic>
#include <list>
#include <memory>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace FiberTaskingLib {

class TaskScheduler;
class TaggedHeap;
class TaggedHeapBackedLinearAllocator;
struct GlobalArgs;


typedef void(*TaskFunction)(FiberTaskingLib::TaskScheduler *g_taskScheduler,
                            FiberTaskingLib::TaggedHeap *g_heap,
                            FiberTaskingLib::TaggedHeapBackedLinearAllocator *g_allocator,
                            void *arg);
/**
 * Creates the correct function signature for a task entry point
 *
 * The function will have the following args:
 *     TaskScheduler *g_taskScheduler,
 *     TaggedHeap *g_heap,
 *     TaggedHeapBackedLinearAllocator *g_allocator,
 *     void *arg
 * where arg == Task::ArgData
 */
#define TASK_ENTRY_POINT(functionName) void functionName(FiberTaskingLib::TaskScheduler *g_taskScheduler, \
                                                         FiberTaskingLib::TaggedHeap *g_heap, \
                                                         FiberTaskingLib::TaggedHeapBackedLinearAllocator *g_allocator, \
                                                         void *arg)


typedef std::atomic_char32_t AtomicCounter;


struct Task {
	TaskFunction Function;
	void *ArgData;
};

#define FIBER_POOL_SIZE 25

/**
 * A class that enables task-based multithreading.
 *
 * Underneath the covers, it uses fibers to allow cores to work on other tasks
 * when the current task is waiting on a synchronization atomic
 *
 * Note: Only one instance of this class should ever exist at a time.
 */
class TaskScheduler {
public:
	TaskScheduler();
	~TaskScheduler();

private:
	DWORD m_numThreads;
	HANDLE *m_threads;

	/**
	 * Holds a task that is ready to to be executed by the worker threads
	 * Counter is the counter for the task(group). It will be decremented when the task completes
	 */
	struct TaskBundle {
		Task Task;
		std::shared_ptr<AtomicCounter> Counter;
	};

	/**
	 * Holds a fiber that is waiting on a counter to be a certain value
	 */
	struct WaitingTask {
		WaitingTask() 
			: Fiber(nullptr), 
			  Counter(nullptr), 
			  Value(0) {
		}
		WaitingTask(void *fiber, AtomicCounter *counter, int value)
			: Fiber(fiber),
			  Counter(counter),
			  Value(value) {
		}

		void *Fiber;
		AtomicCounter *Counter;
		int Value;
	};

	moodycamel::ConcurrentQueue<TaskBundle> m_taskQueue;
	std::list<WaitingTask> m_waitingTasks;
	CRITICAL_SECTION m_waitingTaskLock;

	moodycamel::BlockingConcurrentQueue<void *> m_fiberPool;

	/**
	 * In order to put the current fiber on the waitingTasks list or the fiber pool, we have to
	 * switch to a different fiber. If we naively added ourselves to the list/fiber pool, then
	 * switch to the new fiber, another thread could pop from the list/pool and
	 * try to execute the current fiber before we have time to switch. This leads to stack corruption
	 * and/or general undefined behavior.
	 *
	 * So we use helper fibers to do the store/switch for us. However, we have to have a helper
	 * fiber for each thread. Otherwise, two threads could try to switch to the same helper fiber
	 * at the same time. Again, this leads to stack corruption and/or general undefined behavior.
	 */
	void **m_fiberSwitchingFibers;
	void **m_counterWaitingFibers;

	std::atomic_bool m_quit;

public:
	/**
	 * Creates the fiber pool and spawns worker threads for each (logical) CPU core. Each worker
	 * thread is affinity bound to a single core.
	 *
	 * @param globalArgs    A valid GlobalArgs instance
	 */
	void Initialize(GlobalArgs *globalArgs);

	/**
	 * Adds a task to the internal queue. 
	 *
	 * @param task    The task to queue
	 * @return        An atomic counter corresponding to this task. Initially it will equal 1. When the task completes, it will be decremented.
	 */
	std::shared_ptr<AtomicCounter> AddTask(Task task);
	/**
	 * Adds a group of tasks to the internal queue
	 *
	 * @param numTasks    The number of tasks
	 * @param tasks       The tasks to queue
	 * @return            An atomic counter corresponding to the task group as a whole. Initially it will equal numTasks. When each task completes, it will be decremented.
	 */
	std::shared_ptr<AtomicCounter> AddTasks(uint numTasks, Task *tasks);

	/**
	 * Yields execution to another task until counter == value
	 *
	 * @param counter    The counter to check
	 * @param value      The value to wait for
	 */
	void WaitForCounter(std::shared_ptr<AtomicCounter> &counter, int value);
	/**
	 * Tells all worker threads to quit, then waits for all threads to complete.
	 * Any currently running task will finish before the worker thread returns.
	 *
	 * @return        
	 */
	void Quit();

private:
	/**
	 * Pops the next task off the queue into nextTask. If there are no tasks in the
	 * the queue, it will return false.
	 *
	 * @param nextTask    If the queue is not empty, will be filled with the next task
	 * @return            True: Successfully popped a task out of the queue
	 */
	bool GetNextTask(TaskBundle *nextTask);
	/**
	 * Returns the current fiber back to the fiber pool and switches to fiberToSwitchTo
	 *
	 * Note: As noted above, we use helper fibers to do this switch for us.
	 *
	 * @param fiberToSwitchTo    The fiber to switch to
	 */
	void SwitchFibers(void *fiberToSwitchTo);

	/**
	 * The threadProc function for all worker threads
	 *
	 * @param arg    An instance of ThreadStartArgs
	 * @return       The return status of the thread
	 */
	static uint __stdcall ThreadStart(void *arg);
	/**
	 * The fiberProc function for all fibers in the fiber pool
	 *
	 * @param arg    An instance of GlobalArgs
	 */
	static void __stdcall FiberStart(void *arg);
	/**
	 * The fiberProc function for the fiber switching helper fiber
	 *
	 * @param arg    An instance of TaskScheduler
	 */
	static void __stdcall FiberSwitchStart(void *arg);
	/**
	 * The fiberProc function for the counter wait helper fiber
	 *
	 * @param arg    An instance of TaskScheduler  
	 */
	static void __stdcall CounterWaitStart(void *arg);
};

} // End of namespace FiberTaskingLib
