#pragma once
#include <prayground/prayground.h>
#include "params.h"
#include <map>

using namespace std;

using RaygenRecord = Record<RaygenData>;
using HitgroupRecord = Record<HitgroupData>;
using MissRecord = Record<MissData>;
using EmptyRecord = Record<EmptyData>;

using PathTracingSBT = ShaderBindingTable<RaygenRecord, MissRecord, HitgroupRecord, EmptyRecord, EmptyRecord, 2>;

using Primitive = pair<ShapeInstance, variant<shared_ptr<Material>, shared_ptr<AreaEmitter>>>;

class App : public BaseApp
{
public:
    void setup();
    void update();
    void draw();

    void mouseDragged(float x, float y, int button);
    void mouseScrolled(float xoffset, float yoffset);
private:
    LaunchParams params;
    CUDABuffer<LaunchParams> d_params;
    Pipeline pipeline;
    Context context;
    CUstream stream;
    PathTracingSBT sbt;
    InstanceAccel ias;

    Bitmap result_bitmap;
    FloatBitmap accum_bitmap;
    Camera camera;

    vector<Primitive> primitives;
};