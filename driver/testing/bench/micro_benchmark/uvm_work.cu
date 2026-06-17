#include <cerrno>
#include <cuda_runtime.h>
#include <curand.h>
#include <curand_kernel.h>
#include <iostream>

#define ull unsigned long long

template <typename T, typename P> T cceil(const T &a, const P &b) {
  return (a + b - 1) / b;
}

using namespace std;
__global__ void regular(char *p, ull size, ull iterations_per_block,
                        ull elements_per_thread) {
  ull tid = blockIdx.x * blockDim.x + threadIdx.x;
  ull start_idx = tid * elements_per_thread;
  ull end_idx = min(start_idx + elements_per_thread, size);

  // Each thread iterates 'iterations_per_block' times over its assigned block
  for (ull k = 0; k < iterations_per_block; ++k) {
    for (ull i = start_idx; i < end_idx; ++i) {
      ull cnt = 0;
      // for(ull j=2;j * j <= i; j++) {
      //   if(j % i == 0)
      //     cnt++;
      // }
      p[i] = 'R' + cnt ;
    }
  }
}

/* __global__ void irregular(char *p, ull *offset_indices, ull size,
                          ull iterations_per_block, ull perThreadAccess) {
  ull tid = blockIdx.x * blockDim.x + threadIdx.x;
  // tid is the index in the randomOffset
  // size is the size of p
  // offset_indices has size of totalThreads * sizeof(ull)
  for (ull k = 0; k < iterations_per_block; ++k) {
    ull current_off = (offset_indices[tid] * perThreadAccess) % size;
    for (ull i = 0; i < perThreadAccess; ++i) {
      p[current_off] = 'I' + (char)((i * k) % 26);
      current_off = (offset_indices[current_off] * perThreadAccess + i) % size;
    }
  }
} */
__global__ void irregular(char *p, curandState *rngState, ull size,
                          ull iterations_per_block, ull perThreadAccess,
                          ull seed) {
  ull tid = blockIdx.x * blockDim.x + threadIdx.x;
  curand_init(seed, tid, 0, &rngState[tid]);
  for (int i = 0; i < iterations_per_block; ++i) {
    for (int j = 0; j < perThreadAccess; ++j) {
      double rand_val = curand_uniform_double(&rngState[tid]);
      ull off = (ull)(rand_val * size) % size;
      ull cnt = 0;
      for(ull i=2;i * i <= off; i++) {
        if(i % off == 0)
          cnt++;
      }
      p[off] = 'I' + cnt;
    }
  }
}

void usage() {
  cout << "usage: ./uvm_work [size] [r/i] [seed] [iter] <grid> <block> \n";
  cout << "size - TOTAL cudaMallocManaged size in GB\n";
  cout << "r/i - regular or irregular workload\n";
  cout << "seed - seed for random offset on irregular workload - ONLY ON "
          "IRREGULAR\n";
  cout << "iter - Number of iterations in a pattern\n";
  cout << "Optional start after this \n";
  cout << "grid block - used to launch the kernel. Default <164,32>\n";
}

int main(int argc, char *argv[]) {
  char *devPtr;

  if (argc < 5 || argc > 7) {
    usage();
    return -EINVAL;
  }

  ull size = 0;
  const ull GB = 1ULL << 30;
  sscanf(argv[1], "%llu", &size);
  size *= GB;

  char mode = argv[2][0];
  if (mode != 'r' && mode != 'i') {
    usage();
    return -EINVAL;
  }

  ull seed = 0;
  sscanf(argv[3], "%llu", &seed);

  ull iter = 0;
  sscanf(argv[4], "%llu", &iter);

  ull grid = 164, block = 32;
  if (argc > 5) {
    sscanf(argv[5], "%llu", &grid);
  }

  if (argc > 6) {
    sscanf(argv[6], "%llu", &block);
  }
  ull totalThreads = grid * block;

  if (mode == 'r') {
    ull perThreadHandle = cceil(size, totalThreads);
    cout << "\nGrid: " << grid << "\nBlock: " << block
         << "\ntotalThreads: " << totalThreads
         << "\nperThread: " << perThreadHandle
         << "\niter: "<< iter << "\nSize: "<< size << "\n";
    cudaMallocManaged(&devPtr, size);
    regular<<<grid, block>>>(devPtr, size, iter, perThreadHandle);
  } else {
    curandState *rngStates;
    ull randStateSize = totalThreads * sizeof(curandState);
    cudaMallocManaged(&rngStates, randStateSize);
    ull to_alloc = size - randStateSize;
    cudaMallocManaged(&devPtr, to_alloc);
    ull perThreadHandle = cceil(to_alloc, totalThreads);
    cout << "\nGrid :" << grid << "\nBlock: " << block
         << "\ntotalThreads: " << totalThreads
         << "\nperThread :" << perThreadHandle << "\nSize: " << to_alloc
         << "\n";
    irregular<<<grid, block>>>(devPtr, rngStates, to_alloc, iter,
                               perThreadHandle, seed);
    size = to_alloc;
  }

  cudaDeviceSynchronize();
  for (unsigned long long i = 0; i < size; i += 1024) {
    devPtr[i] += 2;
  }
  cout << "DONE\n";
}
