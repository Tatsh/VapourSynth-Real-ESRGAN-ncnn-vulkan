// realesrgan implemented with ncnn library
#include <iostream>
#include <format>
#include <cstdio>
#include <algorithm>
#include <queue>
#include <clocale>
#include <filesystem>
#include <mutex>
#include <condition_variable>
#include <map>
#include <fstream>

// ncnn
#include <VSHelper4.h>
#include <VapourSynth4.h>
#include <cpu.h>
#include <gpu.h>
#include <platform.h>

#include "realesrgan.h"

namespace fs = std::filesystem;

class Semaphore
{
private:
  int val;
  std::mutex mtx;
  std::condition_variable cv;

public:
  explicit Semaphore(int init_value) : val(init_value) {}

  void wait()
  {
    std::unique_lock<std::mutex> lock(mtx);
    while (val <= 0)
    {
      cv.wait(lock);
    }
    val--;
  }

  void signal()
  {
    std::lock_guard<std::mutex> guard(mtx);
    val++;
    cv.notify_one();
  }
};

struct FilterData
{
  VSNode *node;
  const VSVideoInfo *vi;
  int target_width, target_height;
  RealESRGAN *realesrgan;
  Semaphore *gpuSemaphore;
};

static std::mutex g_lock{};
static int g_filter_instance_count = 0;
static std::map<int, Semaphore *> g_gpu_semaphore;

static void process(const VSFrame *src, VSFrame *dst, const FilterData *const VS_RESTRICT d, const VSAPI *vsapi) noexcept
{
  if (d->vi->format.colorFamily == cfRGB)
  {
    int src_width = vsapi->getFrameWidth(src, 0);
    int src_height = vsapi->getFrameHeight(src, 0);
    int src_stride = static_cast<int>(vsapi->getStride(src, 0) / sizeof(float));
    int dst_stride = static_cast<int>(vsapi->getStride(dst, 0) / sizeof(float));

    const float *srcpR = reinterpret_cast<const float *>(vsapi->getReadPtr(src, 0));
    const float *srcpG = reinterpret_cast<const float *>(vsapi->getReadPtr(src, 1));
    const float *srcpB = reinterpret_cast<const float *>(vsapi->getReadPtr(src, 2));

    float *VS_RESTRICT dstpR = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 0));
    float *VS_RESTRICT dstpG = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 1));
    float *VS_RESTRICT dstpB = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 2));

    d->gpuSemaphore->wait();
    d->realesrgan->process(srcpR, srcpG, srcpB, dstpR, dstpG, dstpB, src_width, src_height, src_stride, dst_stride);
    d->gpuSemaphore->signal();
  }
}

static const VSFrame *VS_CC filterGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
  FilterData *d = static_cast<FilterData *>(instanceData);

  if (activationReason == arInitial)
  {
    vsapi->requestFrameFilter(n, d->node, frameCtx);
  }
  else if (activationReason == arAllFramesReady)
  {
    const VSFrame *src = vsapi->getFrameFilter(n, d->node, frameCtx);
    VSFrame *dst = vsapi->newVideoFrame(&d->vi->format, d->target_width, d->target_height, src, core);

    process(src, dst, d, vsapi);

    vsapi->freeFrame(src);
    return dst;
  }

  return nullptr;
}

static void VS_CC filterFree(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
  FilterData *d = static_cast<FilterData *>(instanceData);
  vsapi->freeNode(d->node);

  delete d->realesrgan;
  delete d;

  std::lock_guard<std::mutex> guard(g_lock);
  g_filter_instance_count--;
  if (g_filter_instance_count == 0)
  {
    ncnn::destroy_gpu_instance();
    for (auto pair : g_gpu_semaphore)
    {
      delete pair.second;
    }
    g_gpu_semaphore.clear();
  }
}

static void VS_CC filterCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
  std::unique_ptr<FilterData> d = std::make_unique<FilterData>();
  int err;

  d->node = vsapi->mapGetNode(in, "clip", 0, 0);
  d->vi = vsapi->getVideoInfo(d->node);

  {
    std::lock_guard<std::mutex> guard(g_lock);

    if (g_filter_instance_count == 0)
    {
      ncnn::create_gpu_instance();
    }

    g_filter_instance_count++;
  }

  try
  {
    if (!vsh::isConstantVideoFormat(d->vi) ||
        d->vi->format.sampleType == stInteger ||
        (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample != 32))
      throw std::string{"only constant format 32 bits float input supported"};

    int scale = vsh::int64ToIntS(vsapi->mapGetInt(in, "scale", 0, &err));
    if (err || scale < 2)
      scale = 2;
    if (scale > 4)
      throw std::string{"model is only supported up to 4x scale"};

    d->target_width = d->vi->width * scale;
    d->target_height = d->vi->height * scale;

    // Model path
    const std::string pluginPath{vsapi->getPluginPath(vsapi->getPluginByID("com.vapoursynth.realesrgan", core))};
    std::string paramPath{pluginPath.substr(0, pluginPath.find_last_of('/'))};
    std::string modelPath{pluginPath.substr(0, pluginPath.find_last_of('/'))};

    int model = vsh::int64ToIntS(vsapi->mapGetInt(in, "model", 0, &err));
    if (err)
      model = 0;

    /*
    /usr/share/realesrgan-ncnn-vulkan/models/realesr-animevideov3-x2.bin
    /usr/share/realesrgan-ncnn-vulkan/models/realesr-animevideov3-x2.param
    /usr/share/realesrgan-ncnn-vulkan/models/realesr-animevideov3-x3.bin
    /usr/share/realesrgan-ncnn-vulkan/models/realesr-animevideov3-x3.param
    /usr/share/realesrgan-ncnn-vulkan/models/realesr-animevideov3-x4.bin
    /usr/share/realesrgan-ncnn-vulkan/models/realesr-animevideov3-x4.param
    /usr/share/realesrgan-x4plus-anime.bin
    /usr/share/realesrgan-x4plus-anime.param
    /usr/share/realesrgan-x4plus.bin
    /usr/share/realesrgan-x4plus.param
    /usr/share/realesrnet-x4plus.bin
    /usr/share/realesrnet-x4plus.param
    */
    if (model == 0)
    {
      paramPath += std::format("/models/realesr-animevideov3-x{}.param", scale).c_str();
      modelPath += std::format("/models/realesr-animevideov3-x{}.bin", scale).c_str();
    }
    else if (model == 1)
    {
      paramPath += "/models/realesrgan-x4plus-anime.param";
      modelPath += "/models/realesrgan-x4plus-anime.bin";
    }
    else if (model == 2)
    {
      paramPath += "/models/realesrgan-x4plus.param";
      modelPath += "/models/realesrgan-x4plus.bin";
    }
    else
      throw std::string{"invalid model type. Try 0, 1, 2"};

    // Check model file readable
    std::ifstream pf(paramPath);
    std::ifstream mf(modelPath);
    if (!pf.good() || !mf.good())
      throw std::string{"can't open model file"};

    // GPU id
    int gpuId = vsh::int64ToIntS(vsapi->mapGetInt(in, "gpu_id", 0, &err));
    if (err)
      gpuId = 0;
    if (gpuId < 0 || gpuId >= ncnn::get_gpu_count())
      throw std::string{"invalid 'gpu_id'"};

    // Tile size
    int tilesize = vsh::int64ToIntS(vsapi->mapGetInt(in, "tilesize", 0, &err));
    if (err)
      tilesize = 100;
    if (tilesize != 0 && tilesize < 32)
      throw std::string{"tilesize must be >= 32 or set as 0"};

    int tilesize_y = vsh::int64ToIntS(vsapi->mapGetInt(in, "tilesize_y", 0, &err));
    if (err)
      tilesize_y = tilesize;
    if (tilesize_y != 0 && tilesize_y < 32)
      throw std::string{"tilesize_y must be >= 32 or set as 0"};

    // More fine-grained tilesize policy here
    uint32_t heap_budget = ncnn::get_gpu_device(gpuId)->get_heap_budget();
    if (tilesize == 0)
    {
      if (heap_budget > 2600)
        tilesize = 400;
      else if (heap_budget > 740)
        tilesize = 200;
      else if (heap_budget > 250)
        tilesize = 100;
      else
        tilesize = 32;
    }

    int gpuThread;
    int customGpuThread = vsh::int64ToIntS(vsapi->mapGetInt(in, "gpu_thread", 0, &err));
    if (customGpuThread > 0)
      gpuThread = customGpuThread;
    else
      gpuThread = vsh::int64ToIntS(ncnn::get_gpu_info(gpuId).transfer_queue_count());
    gpuThread = std::min(gpuThread, vsh::int64ToIntS(ncnn::get_gpu_info(gpuId).compute_queue_count()));

    std::lock_guard<std::mutex> guard(g_lock);
    if (!g_gpu_semaphore.count(gpuId))
      g_gpu_semaphore.insert(std::pair<int, Semaphore *>(gpuId, new Semaphore(gpuThread)));
    d->gpuSemaphore = g_gpu_semaphore.at(gpuId);

    bool tta = !!vsapi->mapGetInt(in, "tta", 0, &err);

    d->realesrgan = new RealESRGAN(gpuId, tta);
    d->realesrgan->scale = scale;
    d->realesrgan->tilesize = tilesize;
    d->realesrgan->prepadding = 10;
    d->realesrgan->load(paramPath, modelPath);
  }
  catch (const std::string &error)
  {
    {
      std::lock_guard<std::mutex> guard(g_lock);

      g_filter_instance_count--;
      if (g_filter_instance_count == 0)
        ncnn::destroy_gpu_instance();
    }

    vsapi->mapSetError(out, ("RealESRGAN: " + error).c_str());
    vsapi->freeNode(d->node);
    return;
  }

  const VSFilterDependency deps[] = {{d->node, rpStrictSpatial}};
  VSVideoInfo dst_vi = *d->vi;
  dst_vi.width = d->target_width;
  dst_vi.height = d->target_height;
  vsapi->createVideoFilter(out, "RealESRGAN", &dst_vi, filterGetFrame, filterFree, fmParallel, deps, 1, d.release(), core);
}

VS_EXTERNAL_API(void)
VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *pluginapi)
{
  pluginapi->configPlugin("com.vapoursynth.realesrgan", "esrgan", "RealESRGAN ncnn Vulkan plugin", VS_MAKE_VERSION(0, 1), VAPOURSYNTH_API_VERSION, 0, plugin);
  pluginapi->registerFunction("RealESRGAN",
               "clip:vnode;"
               "scale:int:opt;"
               "tilesize:int:opt;"
               "model:int:opt;"
               "gpu_id:int:opt;"
               "gpu_thread:int:opt;"
               "tta:int:opt;",
               "clip:vnode;",
               filterCreate, nullptr, plugin);
}
