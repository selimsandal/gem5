#define CL_TARGET_OPENCL_VERSION 120

#include <CL/cl.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const char *rodinia_kernels = R"CLC(
typedef struct
{
    int starting;
    int no_of_edges;
} Node;

typedef struct
{
  float weight;
  long assign;
  float cost;
} Point_Struct;

__kernel void hotspotOpt1(__global float *p, __global float* tIn,
                          __global float *tOut, float sdc,
                          int nx, int ny, int nz,
                          float ce, float cw, float cn, float cs,
                          float ct, float cb, float cc)
{
  float amb_temp = 80.0f;
  int i = get_global_id(0);
  int j = get_global_id(1);
  int c = i + j * nx;
  int xy = nx * ny;
  int W = (i == 0) ? c : c - 1;
  int E = (i == nx - 1) ? c : c + 1;
  int N = (j == 0) ? c : c - nx;
  int S = (j == ny - 1) ? c : c + nx;
  float temp1, temp2, temp3;
  temp1 = temp2 = tIn[c];
  temp3 = tIn[c + xy];
  tOut[c] = cc * temp2 + cw * tIn[W] + ce * tIn[E] + cs * tIn[S]
    + cn * tIn[N] + cb * temp1 + ct * temp3 + sdc * p[c] + ct * amb_temp;
  c += xy; W += xy; E += xy; N += xy; S += xy;
  for (int k = 1; k < nz - 1; ++k) {
      temp1 = temp2;
      temp2 = temp3;
      temp3 = tIn[c + xy];
      tOut[c] = cc * temp2 + cw * tIn[W] + ce * tIn[E] + cs * tIn[S]
        + cn * tIn[N] + cb * temp1 + ct * temp3 + sdc * p[c] + ct * amb_temp;
      c += xy; W += xy; E += xy; N += xy; S += xy;
  }
  temp1 = temp2;
  temp2 = temp3;
  tOut[c] = cc * temp2 + cw * tIn[W] + ce * tIn[E] + cs * tIn[S]
    + cn * tIn[N] + cb * temp1 + ct * temp3 + sdc * p[c] + ct * amb_temp;
}

__kernel void Fan1(__global float *m_dev, __global float *a_dev,
                   __global float *b_dev, const int size, const int t)
{
    int globalId = get_global_id(0);
    if (globalId < size - 1 - t) {
        m_dev[size * (globalId + t + 1) + t] =
            a_dev[size * (globalId + t + 1) + t] / a_dev[size * t + t];
    }
}

__kernel void Fan2(__global float *m_dev, __global float *a_dev,
                   __global float *b_dev, const int size, const int t)
{
    int globalIdx = get_global_id(0);
    int globalIdy = get_global_id(1);
    if (globalIdx < size - 1 - t && globalIdy < size - t) {
        a_dev[size * (globalIdx + 1 + t) + (globalIdy + t)] -=
            m_dev[size * (globalIdx + 1 + t) + t] *
            a_dev[size * t + (globalIdy + t)];
        if (globalIdy == 0) {
            b_dev[globalIdx + 1 + t] -=
                m_dev[size * (globalIdx + 1 + t) + (globalIdy + t)] * b_dev[t];
        }
    }
}

#ifndef FLT_MAX
#define FLT_MAX 3.40282347e+38
#endif
__kernel void kmeans_kernel_c(__global float *feature,
                              __global float *clusters,
                              __global int *membership,
                              int npoints, int nclusters, int nfeatures,
                              int offset, int size)
{
    unsigned int point_id = get_global_id(0);
    int index = 0;
    if (point_id < npoints) {
        float min_dist = FLT_MAX;
        for (int i = 0; i < nclusters; i++) {
            float ans = 0.0f;
            for (int l = 0; l < nfeatures; l++) {
                float diff = feature[l * npoints + point_id] -
                             clusters[i * nfeatures + l];
                ans += diff * diff;
            }
            if (ans < min_dist) {
                min_dist = ans;
                index = i;
            }
        }
        membership[point_id] = index;
    }
}

#define IN_RANGE(x, min, max) ((x) >= (min) && (x) <= (max))
#define MIN(a, b) ((a) <= (b) ? (a) : (b))
__kernel void dynproc_kernel(int iteration, __global int* gpuWall,
                             __global int* gpuSrc, __global int* gpuResults,
                             int cols, int rows, int startStep, int border,
                             int HALO, __local int* prev,
                             __local int* result, __global int* outputBuffer)
{
    int BLOCK_SIZE = get_local_size(0);
    int bx = get_group_id(0);
    int tx = get_local_id(0);
    int small_block_cols = BLOCK_SIZE - (iteration * HALO * 2);
    int blkX = (small_block_cols * bx) - border;
    int blkXmax = blkX + BLOCK_SIZE - 1;
    int xidx = blkX + tx;
    int validXmin = (blkX < 0) ? -blkX : 0;
    int validXmax = (blkXmax > cols - 1) ?
        BLOCK_SIZE - 1 - (blkXmax - cols + 1) : BLOCK_SIZE - 1;
    int W = tx - 1;
    int E = tx + 1;
    W = (W < validXmin) ? validXmin : W;
    E = (E > validXmax) ? validXmax : E;
    bool isValid = IN_RANGE(tx, validXmin, validXmax);
    if (IN_RANGE(xidx, 0, cols - 1)) {
        prev[tx] = gpuSrc[xidx];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
    bool computed = false;
    for (int i = 0; i < iteration; i++) {
        computed = false;
        if (IN_RANGE(tx, i + 1, BLOCK_SIZE - i - 2) && isValid) {
            computed = true;
            int shortest = MIN(prev[W], prev[tx]);
            shortest = MIN(shortest, prev[E]);
            int index = cols * (startStep + i) + xidx;
            result[tx] = shortest + gpuWall[index];
            if (tx == 11 && i == 0) {
                int bufIndex = gpuSrc[xidx];
                outputBuffer[bufIndex] = 1;
            }
        }
        barrier(CLK_LOCAL_MEM_FENCE);
        if (i == iteration - 1) {
            break;
        }
        if (computed) {
            prev[tx] = result[tx];
        }
        barrier(CLK_LOCAL_MEM_FENCE);
    }
    if (computed) {
        gpuResults[xidx] = result[tx];
    }
}

__kernel void BFS_1(const __global Node* g_graph_nodes,
                    const __global int* g_graph_edges,
                    __global char* g_graph_mask,
                    __global char* g_updating_graph_mask,
                    __global char* g_graph_visited,
                    __global int* g_cost,
                    const int no_of_nodes)
{
    int tid = get_global_id(0);
    if (tid < no_of_nodes && g_graph_mask[tid]) {
        g_graph_mask[tid] = false;
        for (int i = g_graph_nodes[tid].starting;
             i < g_graph_nodes[tid].no_of_edges + g_graph_nodes[tid].starting;
             i++) {
            int id = g_graph_edges[i];
            if (!g_graph_visited[id]) {
                g_cost[id] = g_cost[tid] + 1;
                g_updating_graph_mask[id] = true;
            }
        }
    }
}

__kernel void BFS_2(__global char* g_graph_mask,
                    __global char* g_updating_graph_mask,
                    __global char* g_graph_visited,
                    __global char* g_over,
                    const int no_of_nodes)
{
    int tid = get_global_id(0);
    if (tid < no_of_nodes && g_updating_graph_mask[tid]) {
        g_graph_mask[tid] = true;
        g_graph_visited[tid] = true;
        *g_over = true;
        g_updating_graph_mask[tid] = false;
    }
}

__kernel void pgain_kernel(__global Point_Struct *p,
                           __global float *coord_d,
                           __global float *work_mem_d,
                           __global int *center_table_d,
                           __global char *switch_membership_d,
                           __local float *coord_s,
                           int num, int dim, long x, int K)
{
    const int thread_id = get_global_id(0);
    const int local_id = get_local_id(0);
    if (thread_id < num) {
        if (local_id == 0) {
            for (int i = 0; i < dim; i++) {
                coord_s[i] = coord_d[i * num + x];
            }
        }
        barrier(CLK_LOCAL_MEM_FENCE);
        float x_cost = 0.0f;
        for (int i = 0; i < dim; i++) {
            float diff = coord_d[(i * num) + thread_id] - coord_s[i];
            x_cost += diff * diff;
        }
        x_cost = x_cost * p[thread_id].weight;
        float current_cost = p[thread_id].cost;
        int base = thread_id * (K + 1);
        if (x_cost < current_cost) {
            switch_membership_d[thread_id] = '1';
            work_mem_d[base + K] = x_cost - current_cost;
        } else {
            int assign = p[thread_id].assign;
            work_mem_d[base + center_table_d[assign]] += current_cost - x_cost;
        }
    }
}
)CLC";

static void
die(const char *msg, cl_int err)
{
    std::fprintf(stderr, "%s failed: %d\n", msg, err);
    std::exit(1);
}

static void
check(cl_int err, const char *msg)
{
    if (err != CL_SUCCESS) {
        die(msg, err);
    }
}

static size_t
round_up(size_t value, size_t multiple)
{ return ((value + multiple - 1) / multiple) * multiple; }

template <typename T>
static void
set_arg(cl_kernel kernel, cl_uint index, const T &value)
{ check(clSetKernelArg(kernel, index, sizeof(T), &value), "clSetKernelArg"); }

static void
set_mem(cl_kernel kernel, cl_uint index, cl_mem mem)
{
    check(clSetKernelArg(kernel, index, sizeof(cl_mem), &mem),
          "clSetKernelArg");
}

struct ClEnv
{
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    cl_context context = nullptr;
    cl_command_queue queue = nullptr;
    cl_program program = nullptr;

    ClEnv()
    {
        cl_int err = CL_SUCCESS;
        cl_uint num_platforms = 0;
        check(clGetPlatformIDs(0, nullptr, &num_platforms),
              "clGetPlatformIDs");
        if (num_platforms == 0) {
            std::fprintf(stderr, "No OpenCL platform found\n");
            std::exit(1);
        }
        std::vector<cl_platform_id> platforms(num_platforms);
        check(clGetPlatformIDs(num_platforms, platforms.data(), nullptr),
              "clGetPlatformIDs");

        for (auto p : platforms) {
            cl_uint n = 0;
            if (clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, 0, nullptr, &n) ==
                    CL_SUCCESS &&
                n > 0) {
                std::vector<cl_device_id> devs(n);
                check(clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, n, devs.data(),
                                     nullptr),
                      "clGetDeviceIDs GPU");
                platform = p;
                device = devs[0];
                break;
            }
        }
        if (!device) {
            std::fprintf(stderr, "No OpenCL GPU device found\n");
            std::exit(1);
        }

        context = clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err);
        check(err, "clCreateContext");
        queue = clCreateCommandQueue(context, device,
                                     CL_QUEUE_PROFILING_ENABLE, &err);
        check(err, "clCreateCommandQueue");

        const char *src = rodinia_kernels;
        size_t len = std::strlen(rodinia_kernels);
        program = clCreateProgramWithSource(context, 1, &src, &len, &err);
        check(err, "clCreateProgramWithSource");
        err = clBuildProgram(program, 1, &device, "-cl-std=CL1.2", nullptr,
                             nullptr);
        if (err != CL_SUCCESS) {
            size_t log_size = 0;
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0,
                                  nullptr, &log_size);
            std::vector<char> log(log_size + 1, 0);
            clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG,
                                  log_size, log.data(), nullptr);
            std::fprintf(stderr, "OpenCL build log:\n%s\n", log.data());
            die("clBuildProgram", err);
        }

        char name[256] = {};
        clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(name), name, nullptr);
        std::printf("OpenCL device: %s\n", name);
    }

    ~ClEnv()
    {
        if (program) {
            clReleaseProgram(program);
        }
        if (queue) {
            clReleaseCommandQueue(queue);
        }
        if (context) {
            clReleaseContext(context);
        }
    }

    cl_kernel
    kernel(const char *name)
    {
        cl_int err = CL_SUCCESS;
        cl_kernel k = clCreateKernel(program, name, &err);
        check(err, name);
        return k;
    }

    template <typename T>
    cl_mem
    buffer(const std::vector<T> &data, cl_mem_flags flags)
    {
        cl_int err = CL_SUCCESS;
        cl_mem mem = clCreateBuffer(context, flags | CL_MEM_COPY_HOST_PTR,
                                    data.size() * sizeof(T),
                                    const_cast<T *>(data.data()), &err);
        check(err, "clCreateBuffer");
        return mem;
    }

    template <typename T>
    cl_mem
    empty_buffer(size_t count, cl_mem_flags flags)
    {
        cl_int err = CL_SUCCESS;
        cl_mem mem =
            clCreateBuffer(context, flags, count * sizeof(T), nullptr, &err);
        check(err, "clCreateBuffer");
        return mem;
    }

    template <typename T>
    void
    read(cl_mem mem, std::vector<T> &data)
    {
        check(clEnqueueReadBuffer(queue, mem, CL_TRUE, 0,
                                  data.size() * sizeof(T), data.data(), 0,
                                  nullptr, nullptr),
              "clEnqueueReadBuffer");
    }

    double
    launch(cl_kernel kernel, cl_uint dims, const size_t *global,
           const size_t *local, const char *label)
    {
        cl_event event = nullptr;
        check(clEnqueueNDRangeKernel(queue, kernel, dims, nullptr, global,
                                     local, 0, nullptr, &event),
              label);
        check(clFinish(queue), "clFinish");
        cl_ulong start = 0;
        cl_ulong end = 0;
        double ms = 0.0;
        if (clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START,
                                    sizeof(start), &start,
                                    nullptr) == CL_SUCCESS &&
            clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END,
                                    sizeof(end), &end,
                                    nullptr) == CL_SUCCESS &&
            end >= start) {
            ms = static_cast<double>(end - start) * 1.0e-6;
        }
        clReleaseEvent(event);
        std::printf("KERNEL %-18s event_ms=%.6f\n", label, ms);
        return ms;
    }
};

static void
hotspot_cpu(const std::vector<float> &p, const std::vector<float> &tin,
            std::vector<float> &out, float sdc, int nx, int ny, int nz,
            float ce, float cw, float cn, float cs, float ct, float cb,
            float cc)
{
    const float amb = 80.0f;
    const int xy = nx * ny;
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            int c = i + j * nx;
            int W = (i == 0) ? c : c - 1;
            int E = (i == nx - 1) ? c : c + 1;
            int N = (j == 0) ? c : c - nx;
            int S = (j == ny - 1) ? c : c + nx;
            float t1 = tin[c], t2 = tin[c], t3 = tin[c + xy];
            out[c] = cc * t2 + cw * tin[W] + ce * tin[E] + cs * tin[S] +
                     cn * tin[N] + cb * t1 + ct * t3 + sdc * p[c] + ct * amb;
            c += xy;
            W += xy;
            E += xy;
            N += xy;
            S += xy;
            for (int k = 1; k < nz - 1; ++k) {
                t1 = t2;
                t2 = t3;
                t3 = tin[c + xy];
                out[c] = cc * t2 + cw * tin[W] + ce * tin[E] + cs * tin[S] +
                         cn * tin[N] + cb * t1 + ct * t3 + sdc * p[c] +
                         ct * amb;
                c += xy;
                W += xy;
                E += xy;
                N += xy;
                S += xy;
            }
            t1 = t2;
            t2 = t3;
            out[c] = cc * t2 + cw * tin[W] + ce * tin[E] + cs * tin[S] +
                     cn * tin[N] + cb * t1 + ct * t3 + sdc * p[c] + ct * amb;
        }
    }
}

static int
run_hotspot(ClEnv &env, int nx, int ny, int nz)
{
    const int cells = nx * ny * nz;
    std::vector<float> p(cells), tin(cells), tout(cells, 0.0f),
        ref(cells, 0.0f);
    for (int i = 0; i < cells; ++i) {
        p[i] = 0.001f * static_cast<float>((i * 17) % 113);
        tin[i] = 80.0f + 0.01f * static_cast<float>((i * 31) % 211);
    }
    const float sdc = 0.05f;
    const float ce = 0.10f, cw = 0.10f, cn = 0.10f, cs = 0.10f;
    const float ct = 0.05f, cb = 0.05f;
    const float cc = 1.0f - (ce + cw + cn + cs + ct + cb);
    hotspot_cpu(p, tin, ref, sdc, nx, ny, nz, ce, cw, cn, cs, ct, cb, cc);

    cl_kernel k = env.kernel("hotspotOpt1");
    cl_mem pbuf = env.buffer(p, CL_MEM_READ_ONLY);
    cl_mem tinbuf = env.buffer(tin, CL_MEM_READ_ONLY);
    cl_mem outbuf = env.buffer(tout, CL_MEM_READ_WRITE);
    set_mem(k, 0, pbuf);
    set_mem(k, 1, tinbuf);
    set_mem(k, 2, outbuf);
    set_arg(k, 3, sdc);
    set_arg(k, 4, nx);
    set_arg(k, 5, ny);
    set_arg(k, 6, nz);
    set_arg(k, 7, ce);
    set_arg(k, 8, cw);
    set_arg(k, 9, cn);
    set_arg(k, 10, cs);
    set_arg(k, 11, ct);
    set_arg(k, 12, cb);
    set_arg(k, 13, cc);
    size_t global[2] = {static_cast<size_t>(nx), static_cast<size_t>(ny)};
    size_t local[2] = {8, 1};
    double ms = env.launch(k, 2, global, local, "hotspot3D");
    env.read(outbuf, tout);
    int errors = 0;
    float max_abs = 0.0f;
    for (int i = 0; i < cells; ++i) {
        float d = std::fabs(tout[i] - ref[i]);
        max_abs = std::max(max_abs, d);
        if (d > 1e-4f) {
            ++errors;
        }
    }
    std::printf(
        "RESULT hotspot3D %dx%dx%d %s cells=%d event_ms=%.6f max_abs=%g\n", nx,
        ny, nz, errors ? "FAIL" : "PASS", cells, ms, max_abs);
    clReleaseMemObject(pbuf);
    clReleaseMemObject(tinbuf);
    clReleaseMemObject(outbuf);
    clReleaseKernel(k);
    return errors ? 1 : 0;
}

static void
gaussian_init(std::vector<float> &m, std::vector<float> &a,
              std::vector<float> &b, int size)
{
    std::fill(m.begin(), m.end(), 0.0f);
    for (int i = 0; i < size; ++i) {
        for (int j = 0; j < size; ++j) {
            float v =
                1.0f + 0.01f * static_cast<float>((i * 31 + j * 7) % 211);
            if (i == j) {
                v += static_cast<float>(size);
            }
            a[i * size + j] = v;
        }
        b[i] = 0.5f + 0.001f * static_cast<float>((i * 17) % 113);
    }
}

static void
gaussian_cpu_fan1(std::vector<float> &m, const std::vector<float> &a, int size,
                  int t)
{
    for (int i = t + 1; i < size; ++i) {
        m[i * size + t] = a[i * size + t] / a[t * size + t];
    }
}

static void
gaussian_cpu_fan2(const std::vector<float> &m, std::vector<float> &a,
                  std::vector<float> &b, int size, int t)
{
    for (int i = t + 1; i < size; ++i) {
        for (int j = t; j < size; ++j) {
            a[i * size + j] -= m[i * size + t] * a[t * size + j];
        }
        b[i] -= m[i * size + t] * b[t];
    }
}

static int
run_gaussian(ClEnv &env, const std::string &mode, int size)
{
    std::vector<float> m(size * size), a(size * size), b(size);
    std::vector<float> mr(size * size), ar(size * size), br(size);
    gaussian_init(m, a, b, size);
    gaussian_init(mr, ar, br, size);
    if (mode == "Fan1") {
        gaussian_cpu_fan1(mr, ar, size, 0);
    } else if (mode == "Fan2") {
        gaussian_cpu_fan1(mr, ar, size, 0);
        gaussian_cpu_fan2(mr, ar, br, size, 0);
        m = mr;
    } else {
        for (int t = 0; t < size - 1; ++t) {
            gaussian_cpu_fan1(mr, ar, size, t);
            gaussian_cpu_fan2(mr, ar, br, size, t);
        }
    }

    cl_kernel fan1 = env.kernel("Fan1");
    cl_kernel fan2 = env.kernel("Fan2");
    cl_mem mbuf = env.buffer(m, CL_MEM_READ_WRITE);
    cl_mem abuf = env.buffer(a, CL_MEM_READ_WRITE);
    cl_mem bbuf = env.buffer(b, CL_MEM_READ_WRITE);
    double total_ms = 0.0;
    auto launch_fan1 = [&](int t) {
        set_mem(fan1, 0, mbuf);
        set_mem(fan1, 1, abuf);
        set_mem(fan1, 2, bbuf);
        set_arg(fan1, 3, size);
        set_arg(fan1, 4, t);
        size_t gx =
            round_up(static_cast<size_t>(std::max(0, size - 1 - t)), 8);
        size_t global[1] = {gx};
        size_t local[1] = {8};
        total_ms += env.launch(fan1, 1, global, local, "gaussian_Fan1");
    };
    auto launch_fan2 = [&](int t) {
        set_mem(fan2, 0, mbuf);
        set_mem(fan2, 1, abuf);
        set_mem(fan2, 2, bbuf);
        set_arg(fan2, 3, size);
        set_arg(fan2, 4, t);
        size_t global[2] = {
            round_up(static_cast<size_t>(std::max(0, size - 1 - t)), 8),
            static_cast<size_t>(size - t)};
        size_t local[2] = {8, 1};
        total_ms += env.launch(fan2, 2, global, local, "gaussian_Fan2");
    };
    if (mode == "Fan1") {
        launch_fan1(0);
    } else if (mode == "Fan2") {
        launch_fan2(0);
    } else {
        for (int t = 0; t < size - 1; ++t) {
            launch_fan1(t);
            launch_fan2(t);
        }
    }
    env.read(mbuf, m);
    env.read(abuf, a);
    env.read(bbuf, b);

    int errors = 0;
    float max_abs = 0.0f;
    auto cmp = [&](float x, float y) {
        float d = std::fabs(x - y);
        max_abs = std::max(max_abs, d);
        if (d > (size == 64 ? 1e-3f : 1e-4f)) {
            ++errors;
        }
    };
    if (mode == "Fan1") {
        for (int i = 1; i < size; ++i) {
            cmp(m[i * size], mr[i * size]);
        }
    } else if (mode == "Fan2") {
        for (int i = 1; i < size; ++i) {
            for (int j = 0; j < size; ++j) {
                cmp(a[i * size + j], ar[i * size + j]);
            }
            cmp(b[i], br[i]);
        }
    } else {
        for (int i = 0; i < size * size; ++i) {
            cmp(m[i], mr[i]);
            cmp(a[i], ar[i]);
        }
        for (int i = 0; i < size; ++i) {
            cmp(b[i], br[i]);
        }
    }
    std::printf(
        "RESULT gaussian_%s size=%d %s event_ms=%.6f max_abs=%g errors=%d\n",
        mode.c_str(), size, errors ? "FAIL" : "PASS", total_ms, max_abs,
        errors);
    clReleaseMemObject(mbuf);
    clReleaseMemObject(abuf);
    clReleaseMemObject(bbuf);
    clReleaseKernel(fan1);
    clReleaseKernel(fan2);
    return errors ? 1 : 0;
}

static void
kmeans_cpu(const std::vector<float> &feature,
           const std::vector<float> &clusters, std::vector<int> &membership,
           int npoints, int nclusters, int nfeatures)
{
    for (int p = 0; p < npoints; ++p) {
        float min_dist = FLT_MAX;
        int best = 0;
        for (int i = 0; i < nclusters; ++i) {
            float d = 0.0f;
            for (int l = 0; l < nfeatures; ++l) {
                float diff =
                    feature[l * npoints + p] - clusters[i * nfeatures + l];
                d += diff * diff;
            }
            if (d < min_dist) {
                min_dist = d;
                best = i;
            }
        }
        membership[p] = best;
    }
}

static int
run_kmeans(ClEnv &env, int npoints)
{
    const int nclusters = 4;
    const int nfeatures = 4;
    std::vector<float> feature(nfeatures * npoints),
        clusters(nclusters * nfeatures);
    std::vector<int> member(npoints, -1), ref(npoints, -1);
    for (int l = 0; l < nfeatures; ++l) {
        for (int p = 0; p < npoints; ++p) {
            feature[l * npoints + p] =
                static_cast<float>((p * 7 + l * 31) % 41) / 10.0f;
        }
    }
    for (int i = 0; i < nclusters; ++i) {
        for (int l = 0; l < nfeatures; ++l) {
            clusters[i * nfeatures + l] =
                static_cast<float>(i * 11 + l * 5) / 10.0f;
        }
    }
    kmeans_cpu(feature, clusters, ref, npoints, nclusters, nfeatures);
    cl_kernel k = env.kernel("kmeans_kernel_c");
    cl_mem fbuf = env.buffer(feature, CL_MEM_READ_ONLY);
    cl_mem cbuf = env.buffer(clusters, CL_MEM_READ_ONLY);
    cl_mem mbuf = env.buffer(member, CL_MEM_READ_WRITE);
    int offset = 0, size = npoints;
    set_mem(k, 0, fbuf);
    set_mem(k, 1, cbuf);
    set_mem(k, 2, mbuf);
    set_arg(k, 3, npoints);
    set_arg(k, 4, nclusters);
    set_arg(k, 5, nfeatures);
    set_arg(k, 6, offset);
    set_arg(k, 7, size);
    size_t global[1] = {round_up(static_cast<size_t>(npoints), 8)};
    size_t local[1] = {8};
    double ms = env.launch(k, 1, global, local, "kmeans");
    env.read(mbuf, member);
    int errors = 0;
    for (int i = 0; i < npoints; ++i) {
        if (member[i] != ref[i]) {
            ++errors;
        }
    }
    std::printf("RESULT kmeans npoints=%d %s event_ms=%.6f errors=%d\n",
                npoints, errors ? "FAIL" : "PASS", ms, errors);
    clReleaseMemObject(fbuf);
    clReleaseMemObject(cbuf);
    clReleaseMemObject(mbuf);
    clReleaseKernel(k);
    return errors ? 1 : 0;
}

static void
pathfinder_cpu(const std::vector<int> &wall, const std::vector<int> &src,
               std::vector<int> &result, int cols)
{
    for (int x = 0; x < cols; ++x) {
        int w = (x == 0) ? x : x - 1;
        int e = (x == cols - 1) ? x : x + 1;
        int shortest = std::min(src[w], std::min(src[x], src[e]));
        result[x] = shortest + wall[x];
    }
}

static int
run_pathfinder(ClEnv &env, int cols, int rows)
{
    const int block = 8, iteration = 1, halo = 1;
    const int effective = block - 2 * halo;
    const int groups = (cols + effective - 1) / effective;
    std::vector<int> wall(rows * cols), src(cols), res(cols, -1), ref(cols),
        dbg(64, 0);
    for (int i = 0; i < rows * cols; ++i) {
        wall[i] = (i * 13 + 7) % 31;
    }
    for (int x = 0; x < cols; ++x) {
        src[x] = (x * 11) % 17;
    }
    pathfinder_cpu(wall, src, ref, cols);
    cl_kernel k = env.kernel("dynproc_kernel");
    cl_mem wbuf = env.buffer(wall, CL_MEM_READ_ONLY);
    cl_mem sbuf = env.buffer(src, CL_MEM_READ_ONLY);
    cl_mem rbuf = env.buffer(res, CL_MEM_READ_WRITE);
    cl_mem dbuf = env.buffer(dbg, CL_MEM_READ_WRITE);
    int start = 0;
    set_arg(k, 0, iteration);
    set_mem(k, 1, wbuf);
    set_mem(k, 2, sbuf);
    set_mem(k, 3, rbuf);
    set_arg(k, 4, cols);
    set_arg(k, 5, rows);
    set_arg(k, 6, start);
    set_arg(k, 7, halo);
    set_arg(k, 8, halo);
    check(clSetKernelArg(k, 9, block * sizeof(int), nullptr),
          "clSetKernelArg local prev");
    check(clSetKernelArg(k, 10, block * sizeof(int), nullptr),
          "clSetKernelArg local result");
    set_mem(k, 11, dbuf);
    size_t global[1] = {static_cast<size_t>(groups * block)};
    size_t local[1] = {static_cast<size_t>(block)};
    double ms = env.launch(k, 1, global, local, "pathfinder");
    env.read(rbuf, res);
    int errors = 0, compared = 0;
    for (int x = 0; x < cols; ++x) {
        int block_x = x % effective;
        bool edge = (block_x == effective - 1) || (x != 0 && block_x == 0);
        if (edge) {
            continue;
        }
        ++compared;
        if (res[x] != ref[x]) {
            ++errors;
        }
    }
    std::printf("RESULT pathfinder cols=%d rows=%d %s compared=%d "
                "event_ms=%.6f errors=%d\n",
                cols, rows, errors ? "FAIL" : "PASS", compared, ms, errors);
    clReleaseMemObject(wbuf);
    clReleaseMemObject(sbuf);
    clReleaseMemObject(rbuf);
    clReleaseMemObject(dbuf);
    clReleaseKernel(k);
    return errors ? 1 : 0;
}

struct BfsNode
{
    int32_t starting;
    int32_t no_of_edges;
};

static void
bfs_init(int n, std::vector<BfsNode> &nodes, std::vector<int> &edges,
         std::vector<int8_t> &mask, std::vector<int8_t> &updating,
         std::vector<int8_t> &visited, std::vector<int> &cost)
{
    std::fill(edges.begin(), edges.end(), 0);
    if (n == 16) {
        BfsNode init_nodes[16] = {{0, 7},  {7, 2},  {9, 1},  {10, 1},
                                  {11, 1}, {12, 1}, {13, 1}, {14, 1},
                                  {15, 0}, {15, 0}, {15, 0}, {15, 0},
                                  {15, 0}, {15, 0}, {15, 0}, {15, 0}};
        int init_edges[15] = {1, 2,  3,  4,  5,  6,  7, 8,
                              9, 10, 11, 12, 13, 14, 15};
        std::copy(init_nodes, init_nodes + 16, nodes.begin());
        std::copy(init_edges, init_edges + 15, edges.begin());
    } else {
        int edge_idx = 0;
        for (int i = 0; i < n; ++i) {
            nodes[i].starting = edge_idx;
            nodes[i].no_of_edges = 0;
            int left = 2 * i + 1, right = 2 * i + 2;
            if (left < n) {
                edges[edge_idx++] = left;
                nodes[i].no_of_edges++;
            }
            if (right < n) {
                edges[edge_idx++] = right;
                nodes[i].no_of_edges++;
            }
        }
    }
    std::fill(mask.begin(), mask.end(), 0);
    std::fill(updating.begin(), updating.end(), 0);
    std::fill(visited.begin(), visited.end(), 0);
    std::fill(cost.begin(), cost.end(), -1);
    mask[0] = 1;
    visited[0] = 1;
    cost[0] = 0;
}

static int
bfs_cpu(int n, const std::vector<BfsNode> &nodes,
        const std::vector<int> &edges, std::vector<int8_t> &mask,
        std::vector<int8_t> &updating, std::vector<int8_t> &visited,
        std::vector<int> &cost)
{
    int iterations = 0;
    int8_t stop;
    do {
        stop = 0;
        for (int tid = 0; tid < n; ++tid) {
            if (mask[tid]) {
                mask[tid] = 0;
                for (int i = nodes[tid].starting;
                     i < nodes[tid].starting + nodes[tid].no_of_edges; ++i) {
                    int id = edges[i];
                    if (!visited[id]) {
                        cost[id] = cost[tid] + 1;
                        updating[id] = 1;
                    }
                }
            }
        }
        for (int tid = 0; tid < n; ++tid) {
            if (updating[tid]) {
                mask[tid] = 1;
                visited[tid] = 1;
                stop = 1;
                updating[tid] = 0;
            }
        }
        ++iterations;
    } while (stop);
    return iterations;
}

static int
run_bfs(ClEnv &env, int n)
{
    const int edge_words = ((n - 1 + 7) / 8) * 8;
    const int mask_bytes = ((n + 31) / 32) * 32;
    std::vector<BfsNode> nodes(n), ref_nodes(n);
    std::vector<int> edges(edge_words), ref_edges(edge_words), cost(n),
        ref_cost(n);
    std::vector<int8_t> mask(mask_bytes), upd(mask_bytes), vis(mask_bytes);
    std::vector<int8_t> ref_mask(mask_bytes), ref_upd(mask_bytes),
        ref_vis(mask_bytes);
    bfs_init(n, nodes, edges, mask, upd, vis, cost);
    ref_nodes = nodes;
    ref_edges = edges;
    ref_mask = mask;
    ref_upd = upd;
    ref_vis = vis;
    ref_cost = cost;
    int cpu_iterations =
        bfs_cpu(n, ref_nodes, ref_edges, ref_mask, ref_upd, ref_vis, ref_cost);
    cl_kernel bfs1 = env.kernel("BFS_1");
    cl_kernel bfs2 = env.kernel("BFS_2");
    cl_mem nbuf = env.buffer(nodes, CL_MEM_READ_ONLY);
    cl_mem ebuf = env.buffer(edges, CL_MEM_READ_ONLY);
    cl_mem mbuf = env.buffer(mask, CL_MEM_READ_WRITE);
    cl_mem ubuf = env.buffer(upd, CL_MEM_READ_WRITE);
    cl_mem vbuf = env.buffer(vis, CL_MEM_READ_WRITE);
    cl_mem cbuf = env.buffer(cost, CL_MEM_READ_WRITE);
    cl_mem obuf = env.empty_buffer<int8_t>(32, CL_MEM_READ_WRITE);
    size_t global[1] = {round_up(static_cast<size_t>(n), 8)};
    size_t local[1] = {8};
    double total_ms = 0.0;
    int gpu_iterations = 0;
    int8_t over_host[32] = {};
    do {
        std::memset(over_host, 0, sizeof(over_host));
        check(clEnqueueWriteBuffer(env.queue, obuf, CL_TRUE, 0,
                                   sizeof(over_host), over_host, 0, nullptr,
                                   nullptr),
              "clEnqueueWriteBuffer over");
        set_mem(bfs1, 0, nbuf);
        set_mem(bfs1, 1, ebuf);
        set_mem(bfs1, 2, mbuf);
        set_mem(bfs1, 3, ubuf);
        set_mem(bfs1, 4, vbuf);
        set_mem(bfs1, 5, cbuf);
        set_arg(bfs1, 6, n);
        total_ms += env.launch(bfs1, 1, global, local, "bfs_BFS_1");
        set_mem(bfs2, 0, mbuf);
        set_mem(bfs2, 1, ubuf);
        set_mem(bfs2, 2, vbuf);
        set_mem(bfs2, 3, obuf);
        set_arg(bfs2, 4, n);
        total_ms += env.launch(bfs2, 1, global, local, "bfs_BFS_2");
        check(clEnqueueReadBuffer(env.queue, obuf, CL_TRUE, 0,
                                  sizeof(over_host), over_host, 0, nullptr,
                                  nullptr),
              "clEnqueueReadBuffer over");
        ++gpu_iterations;
        if (gpu_iterations > 64) {
            break;
        }
    } while (over_host[0] != 0);
    env.read(mbuf, mask);
    env.read(ubuf, upd);
    env.read(vbuf, vis);
    env.read(cbuf, cost);
    int errors = 0;
    for (int i = 0; i < n; ++i) {
        if (cost[i] != ref_cost[i]) {
            ++errors;
        }
    }
    for (int i = 0; i < mask_bytes; ++i) {
        if (mask[i] != ref_mask[i] || upd[i] != ref_upd[i] ||
            vis[i] != ref_vis[i]) {
            ++errors;
        }
    }
    if (gpu_iterations != cpu_iterations) {
        ++errors;
    }
    std::printf("RESULT bfs nodes=%d %s gpu_iter=%d cpu_iter=%d event_ms=%.6f "
                "errors=%d\n",
                n, errors ? "FAIL" : "PASS", gpu_iterations, cpu_iterations,
                total_ms, errors);
    clReleaseMemObject(nbuf);
    clReleaseMemObject(ebuf);
    clReleaseMemObject(mbuf);
    clReleaseMemObject(ubuf);
    clReleaseMemObject(vbuf);
    clReleaseMemObject(cbuf);
    clReleaseMemObject(obuf);
    clReleaseKernel(bfs1);
    clReleaseKernel(bfs2);
    return errors ? 1 : 0;
}

struct StreamPoint
{
    float weight;
    int64_t assign;
    float cost;
};

static float
sc_dist(const std::vector<float> &coords, int dim, int a, int b, int num)
{
    float d = 0.0f;
    for (int i = 0; i < dim; ++i) {
        float diff = coords[i * num + a] - coords[i * num + b];
        d += diff * diff;
    }
    return d;
}

static void
sc_build_center_table(const std::vector<uint8_t> &is_center,
                      std::vector<int> &center_table)
{
    int count = 0;
    for (size_t i = 0; i < is_center.size(); ++i) {
        center_table[i] = is_center[i] ? count++ : -1;
    }
}

static void
sc_init(int num, int dim, std::vector<float> &coords,
        std::vector<StreamPoint> &points, std::vector<uint8_t> &is_center,
        std::vector<int> &center_table, int &x, int64_t &numcenters)
{
    const float centers[3][3] = {{0, 0, 0}, {10, 10, 10}, {5, 5, 5}};
    std::fill(is_center.begin(), is_center.end(), 0);
    int center1 = num / 3;
    int candidate = (2 * num) / 3;
    is_center[0] = 1;
    is_center[center1] = 1;
    x = candidate;
    numcenters = 2;
    for (int i = 0; i < num; ++i) {
        int cluster = i % 3;
        for (int d = 0; d < dim; ++d) {
            float jitter =
                0.05f * static_cast<float>(((i * 17 + d * 7) % 11) - 5);
            coords[d * num + i] = centers[cluster][d] + jitter;
        }
        points[i].weight = 1.0f + 0.05f * static_cast<float>(i % 5);
        points[i].assign = 0;
        points[i].cost = 0.0f;
    }
    sc_build_center_table(is_center, center_table);
    for (int i = 0; i < num; ++i) {
        int assign = 0;
        float best = sc_dist(coords, dim, i, 0, num);
        float alt = sc_dist(coords, dim, i, center1, num);
        if (alt < best) {
            best = alt;
            assign = center1;
        }
        points[i].assign = assign;
        points[i].cost = points[i].weight * best;
    }
}

static void
sc_kernel_cpu(const std::vector<StreamPoint> &points,
              const std::vector<float> &coords, std::vector<float> &work,
              const std::vector<int> &center_table, std::vector<int8_t> &sw,
              int num, int dim, int64_t x, int k)
{
    std::fill(work.begin(), work.end(), 0.0f);
    std::fill(sw.begin(), sw.end(), 0);
    for (int tid = 0; tid < num; ++tid) {
        float x_cost = sc_dist(coords, dim, tid, static_cast<int>(x), num) *
                       points[tid].weight;
        float current = points[tid].cost;
        int base = tid * (k + 1);
        if (x_cost < current) {
            sw[tid] = '1';
            work[base + k] = x_cost - current;
        } else {
            int assign = static_cast<int>(points[tid].assign);
            work[base + center_table[assign]] += current - x_cost;
        }
    }
}

static float
sc_post(std::vector<StreamPoint> &points, float z, int64_t &numcenters,
        int num, std::vector<uint8_t> &is_center,
        const std::vector<int> &center_table, const std::vector<int8_t> &sw,
        std::vector<float> &work, const std::vector<float> &coords, int dim,
        int64_t x)
{
    int k = static_cast<int>(numcenters);
    int numclose = 0;
    float gl_cost = z;
    std::vector<float> gl_lower(k, 0.0f);
    for (int i = 0; i < num; ++i) {
        if (is_center[i]) {
            float low = z;
            for (int j = 0; j < num; ++j) {
                low += work[j * (k + 1) + center_table[i]];
            }
            gl_lower[center_table[i]] = low;
            if (low > 0) {
                numclose++;
                work[i * (k + 1) + k] -= low;
            }
        }
        gl_cost += work[i * (k + 1) + k];
    }
    if (gl_cost < 0.0f) {
        for (int i = 0; i < num; ++i) {
            int assign = static_cast<int>(points[i].assign);
            int close_center = gl_lower[center_table[assign]] > 0.0f;
            if (sw[i] == '1' || close_center) {
                points[i].cost =
                    points[i].weight *
                    sc_dist(coords, dim, i, static_cast<int>(x), num);
                points[i].assign = x;
            }
        }
        for (int i = 0; i < num; ++i) {
            if (is_center[i] && gl_lower[center_table[i]] > 0.0f) {
                is_center[i] = 0;
            }
        }
        is_center[x] = 1;
        numcenters = numcenters + 1 - numclose;
    } else {
        gl_cost = 0.0f;
    }
    return -gl_cost;
}

static int
run_streamcluster(ClEnv &env, int num)
{
    const int dim = 3;
    const int switch_bytes = ((num + 31) / 32) * 32;
    std::vector<float> coords(num * dim), cpu_work(num * 3), gpu_work(num * 3);
    std::vector<int> center_table(num);
    std::vector<int8_t> cpu_sw(switch_bytes), gpu_sw(switch_bytes);
    std::vector<StreamPoint> cpu_points(num), gpu_points(num),
        kernel_points(num);
    std::vector<uint8_t> cpu_center(num), gpu_center(num);
    int x = 0;
    int64_t cpu_centers = 0, gpu_centers = 0;
    sc_init(num, dim, coords, cpu_points, cpu_center, center_table, x,
            cpu_centers);
    gpu_points = cpu_points;
    kernel_points = cpu_points;
    gpu_center = cpu_center;
    gpu_centers = cpu_centers;
    sc_kernel_cpu(cpu_points, coords, cpu_work, center_table, cpu_sw, num, dim,
                  x, static_cast<int>(cpu_centers));
    float cpu_gain = sc_post(cpu_points, 0.25f, cpu_centers, num, cpu_center,
                             center_table, cpu_sw, cpu_work, coords, dim, x);
    cl_kernel k = env.kernel("pgain_kernel");
    cl_mem pbuf = env.buffer(kernel_points, CL_MEM_READ_WRITE);
    cl_mem coordbuf = env.buffer(coords, CL_MEM_READ_ONLY);
    cl_mem workbuf = env.buffer(gpu_work, CL_MEM_READ_WRITE);
    cl_mem ctabbuf = env.buffer(center_table, CL_MEM_READ_ONLY);
    cl_mem swbuf = env.buffer(gpu_sw, CL_MEM_READ_WRITE);
    set_mem(k, 0, pbuf);
    set_mem(k, 1, coordbuf);
    set_mem(k, 2, workbuf);
    set_mem(k, 3, ctabbuf);
    set_mem(k, 4, swbuf);
    check(clSetKernelArg(k, 5, dim * sizeof(float), nullptr),
          "clSetKernelArg local coord");
    int64_t x_arg = x;
    int centers_arg = static_cast<int>(gpu_centers);
    set_arg(k, 6, num);
    set_arg(k, 7, dim);
    set_arg(k, 8, x_arg);
    set_arg(k, 9, centers_arg);
    size_t global[1] = {round_up(static_cast<size_t>(num), 8)};
    size_t local[1] = {8};
    double ms = env.launch(k, 1, global, local, "streamcluster");
    env.read(workbuf, gpu_work);
    env.read(swbuf, gpu_sw);
    float gpu_gain = sc_post(gpu_points, 0.25f, gpu_centers, num, gpu_center,
                             center_table, gpu_sw, gpu_work, coords, dim, x);
    int errors = 0;
    float max_abs = 0.0f;
    for (size_t i = 0; i < gpu_sw.size(); ++i) {
        if (gpu_sw[i] != cpu_sw[i]) {
            ++errors;
        }
    }
    for (size_t i = 0; i < gpu_work.size(); ++i) {
        float d = std::fabs(gpu_work[i] - cpu_work[i]);
        max_abs = std::max(max_abs, d);
        if (d > 1e-4f) {
            ++errors;
        }
    }
    for (int i = 0; i < num; ++i) {
        if (gpu_points[i].assign != cpu_points[i].assign) {
            ++errors;
        }
        if (std::fabs(gpu_points[i].cost - cpu_points[i].cost) > 1e-4f) {
            ++errors;
        }
        if (gpu_center[i] != cpu_center[i]) {
            ++errors;
        }
    }
    if (gpu_centers != cpu_centers || std::fabs(gpu_gain - cpu_gain) > 1e-4f) {
        ++errors;
    }
    std::printf("RESULT streamcluster points=%d %s event_ms=%.6f max_abs=%g "
                "errors=%d\n",
                num, errors ? "FAIL" : "PASS", ms, max_abs, errors);
    clReleaseMemObject(pbuf);
    clReleaseMemObject(coordbuf);
    clReleaseMemObject(workbuf);
    clReleaseMemObject(ctabbuf);
    clReleaseMemObject(swbuf);
    clReleaseKernel(k);
    return errors ? 1 : 0;
}

static void
usage(const char *argv0)
{
    std::fprintf(stderr,
                 "Usage:\n"
                 "  %s hotspot3D [nx ny nz]\n"
                 "  %s gaussian [Fan1|Fan2|full] [size]\n"
                 "  %s kmeans [points]\n"
                 "  %s pathfinder [cols rows]\n"
                 "  %s bfs [nodes]\n"
                 "  %s streamcluster [points]\n"
                 "  %s all-quick | all-perf\n",
                 argv0, argv0, argv0, argv0, argv0, argv0, argv0);
}

int
main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    ClEnv env;
    std::string cmd = argv[1];
    int rc = 0;
    if (cmd == "hotspot3D") {
        int nx = argc > 2 ? std::atoi(argv[2]) : 32;
        int ny = argc > 3 ? std::atoi(argv[3]) : 32;
        int nz = argc > 4 ? std::atoi(argv[4]) : 8;
        rc = run_hotspot(env, nx, ny, nz);
    } else if (cmd == "gaussian") {
        std::string mode = argc > 2 ? argv[2] : "Fan1";
        int size = argc > 3 ? std::atoi(argv[3]) : 32;
        rc = run_gaussian(env, mode, size);
    } else if (cmd == "kmeans") {
        int points = argc > 2 ? std::atoi(argv[2]) : 64;
        rc = run_kmeans(env, points);
    } else if (cmd == "pathfinder") {
        int cols = argc > 2 ? std::atoi(argv[2]) : 24;
        int rows = argc > 3 ? std::atoi(argv[3]) : 4;
        rc = run_pathfinder(env, cols, rows);
    } else if (cmd == "bfs") {
        int nodes = argc > 2 ? std::atoi(argv[2]) : 16;
        rc = run_bfs(env, nodes);
    } else if (cmd == "streamcluster") {
        int points = argc > 2 ? std::atoi(argv[2]) : 16;
        rc = run_streamcluster(env, points);
    } else if (cmd == "all-quick") {
        rc |= run_hotspot(env, 32, 32, 8);
        rc |= run_gaussian(env, "Fan1", 32);
        rc |= run_gaussian(env, "Fan2", 32);
        rc |= run_gaussian(env, "full", 32);
        rc |= run_kmeans(env, 64);
        rc |= run_kmeans(env, 128);
        rc |= run_kmeans(env, 256);
        rc |= run_pathfinder(env, 24, 4);
        rc |= run_bfs(env, 16);
        rc |= run_streamcluster(env, 16);
    } else if (cmd == "all-perf") {
        rc |= run_hotspot(env, 128, 128, 8);
        rc |= run_gaussian(env, "full", 64);
        rc |= run_kmeans(env, 8192);
        rc |= run_pathfinder(env, 1536, 4);
        rc |= run_bfs(env, 256);
        rc |= run_streamcluster(env, 1024);
    } else {
        usage(argv[0]);
        return 1;
    }
    return rc;
}
