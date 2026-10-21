#include "o1heap.h"
#include "stdio.h"

#define BUFFER_SIZE 10000
char buffer[10000];

int main(int argc, char **argv) {
    // Code to be executed
    O1HeapDiagnostics diagnostics;
    O1HeapInstance* heap = o1heapInit(buffer, BUFFER_SIZE);
    
    {
	void* memory[10];
	for(int i=0; i<10; i++)
	{
	    memory[i] = o1heapAllocate(heap, 128);
	    printf("%3d: %x\n", memory[i]);
	}
	diagnostics = o1heapGetDiagnostics(heap);
	printf("alloc 10*128: %2d %8d %8d %8d %8d %8d\n", o1heapDoInvariantsHold(heap), 
		diagnostics.capacity, diagnostics.allocated, diagnostics.peak_allocated, diagnostics.peak_request_size, diagnostics.oom_count);
	for(int i=0; i<10; i++)
	{
	    o1heapFree(heap, memory[i]);
	}
	diagnostics = o1heapGetDiagnostics(heap);
	printf("alloc 10*128: %2d %8d %8d %8d %8d %8d\n", o1heapDoInvariantsHold(heap), 
		diagnostics.capacity, diagnostics.allocated, diagnostics.peak_allocated, diagnostics.peak_request_size, diagnostics.oom_count);
    }
    
    {
	void* memory[10];
	for(int i=0; i<10; i++)
	{
	    memory[i] = o1heapAllocate(heap, 512);
	    printf("%3d: %x\n", memory[i]);
	}
	diagnostics = o1heapGetDiagnostics(heap);
	printf("alloc 10*512: %2d %8d %8d %8d %8d %8d\n", o1heapDoInvariantsHold(heap), 
		diagnostics.capacity, diagnostics.allocated, diagnostics.peak_allocated, diagnostics.peak_request_size, diagnostics.oom_count);
	for(int i=0; i<10; i++)
	{
	    o1heapFree(heap, memory[i]);
	}
	diagnostics = o1heapGetDiagnostics(heap);
	printf("alloc 10*512: %2d %8d %8d %8d %8d %8d\n", o1heapDoInvariantsHold(heap), 
		diagnostics.capacity, diagnostics.allocated, diagnostics.peak_allocated, diagnostics.peak_request_size, diagnostics.oom_count);
    }
    
    {
	void* memory[10];
	for(int i=0; i<10; i++)
	{
	    memory[i] = o1heapAllocate(heap, 128);
	    printf("%3d: %x\n", memory[i]);
	}
	printf("alloc 10*128: %2d %8d %8d %8d %8d %8d\n", o1heapDoInvariantsHold(heap), 
		diagnostics.capacity, diagnostics.allocated, diagnostics.peak_allocated, diagnostics.peak_request_size, diagnostics.oom_count);
	for(int i=9; i>=0; i--)
	{
	    o1heapFree(heap, memory[i]);
	}
	printf("alloc 10*128: %2d %8d %8d %8d %8d %8d\n", o1heapDoInvariantsHold(heap), 
		diagnostics.capacity, diagnostics.allocated, diagnostics.peak_allocated, diagnostics.peak_request_size, diagnostics.oom_count);
    }
    
    {
	void* memory[10];
	for(int i=0; i<10; i++)
	{
	    memory[i] = o1heapAllocate(heap, 512);
	    printf("%3d: %x\n", memory[i]);
	}
	diagnostics = o1heapGetDiagnostics(heap);
	printf("alloc 10*512: %2d %8d %8d %8d %8d %8d\n", o1heapDoInvariantsHold(heap), 
		diagnostics.capacity, diagnostics.allocated, diagnostics.peak_allocated, diagnostics.peak_request_size, diagnostics.oom_count);
	for(int i=9; i>=0; i--)
	{
	    o1heapFree(heap, memory[i]);
	}
	diagnostics = o1heapGetDiagnostics(heap);
	printf("alloc 10*512: %2d %8d %8d %8d %8d %8d\n", o1heapDoInvariantsHold(heap), 
		diagnostics.capacity, diagnostics.allocated, diagnostics.peak_allocated, diagnostics.peak_request_size, diagnostics.oom_count);
    }
    return 0; 
}
