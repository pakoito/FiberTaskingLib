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

#include "fiber_tasking_lib/task_scheduler.h"
#include "fiber_tasking_lib/global_args.h"

#include <EASTL/string.h>
typedef eastl::basic_string<char, FiberTaskingLib::TaggedHeapBackedLinearAllocator> EastlStringWithCustomAlloc;

#include <cstdio>


TASK_ENTRY_POINT(SecondLevel) {
	volatile int k = 0;
	for (uint j = 0; j < 100000; ++j) {
		k += 1;
	}

	EastlStringWithCustomAlloc *firstArg = (EastlStringWithCustomAlloc *)arg;

	//printf("%s second\n", firstArg->c_str());
}

TASK_ENTRY_POINT(FirstLevel) {
	volatile int k = 0;
	for (uint j = 0; j < 10000000; ++j) {
		k += 1;
	}

	FiberTaskingLib::Task tasks[10];
	for (uint i = 0; i < 10; ++i) {
		EastlStringWithCustomAlloc *newArg = new(g_allocator->allocate(sizeof(EastlStringWithCustomAlloc))) EastlStringWithCustomAlloc("first", *g_allocator);
		tasks[i] = {SecondLevel, newArg};
	}

	std::shared_ptr<FiberTaskingLib::AtomicCounter> counter = g_taskScheduler->AddTasks(10, tasks);
	g_taskScheduler->WaitForCounter(counter, 0);
}


int main() {
	FiberTaskingLib::GlobalArgs *globalArgs = new FiberTaskingLib::GlobalArgs();
	globalArgs->TaskScheduler.Initialize(globalArgs);
	globalArgs->Allocator.init(&globalArgs->Heap, 1234);


	for (uint j = 0; j < 10; ++j) {
		FiberTaskingLib::Task tasks[10];
		for (uint i = 0; i < 10; ++i) {
			tasks[i] = {FirstLevel, nullptr};
		}

		std::shared_ptr<FiberTaskingLib::AtomicCounter> counter = globalArgs->TaskScheduler.AddTasks(10, tasks);
		globalArgs->TaskScheduler.WaitForCounter(counter, 0);

		globalArgs->Heap.FreeAllPagesWithId(1234);
		globalArgs->Allocator.reset(1234);
	}

	globalArgs->TaskScheduler.Quit();
	globalArgs->Allocator.destroy();
	delete globalArgs;

	return 1;
}
