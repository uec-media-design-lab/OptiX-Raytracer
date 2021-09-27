#include "app.h"

#define GROUP_RENDER 0

void App::initResultBufferOnDevice()
{
    params.subframe_index = 0;

    result_bitmap.allocateDevicePtr();
    accum_bitmap.allocateDevicePtr();

    params.result_buffer = reinterpret_cast<uchar4*>(result_bitmap.devicePtr());
    params.accum_buffer = reinterpret_cast<float4*>(accum_bitmap.devicePtr());

    CUDA_SYNC_CHECK();
}

void App::handleCameraUpdate()
{
    if (!camera_update)
        return;
    camera_update = false;

    float3 U, V, W;
    camera.UVWFrame(U, V, W);

    RaygenRecord* rg_record = reinterpret_cast<RaygenRecord*>(sbt.raygenRecord());
    RaygenData rg_data;
    rg_data.camera =
    {
        .origin = camera.origin(),
        .lookat = camera.lookat(),
        .U = U, 
        .V = V, 
        .W = W,
        .farclip = camera.farClip()
    };

    CUDA_CHECK(cudaMemcpy(
        reinterpret_cast<void*>(&rg_record->data),
        &rg_data, sizeof(RaygenData),
        cudaMemcpyHostToDevice
    ));

    initResultBufferOnDevice();
}

// ----------------------------------------------------------------
void App::setup()
{
    // CUDAの初期化とOptixDeviceContextの生成
    stream = 0;
    CUDA_CHECK(cudaFree(0));
    OPTIX_CHECK(optixInit());
    context.create();

    // Instance acceleration structureの初期化
    scene_ias = InstanceAccel{InstanceAccel::Type::Instances};

    OptixMotionOptions motion_options;
    motion_options.numKeys = 2;
    motion_options.timeBegin = 0.0f;
    motion_options.timeEnd = 1.0f;
    motion_options.flags = OPTIX_MOTION_FLAG_NONE;
    scene_ias.setMotionOptions(motion_options);

    // パイプラインの設定
    pipeline.setLaunchVariableName("params");
    pipeline.setDirectCallableDepth(5);
    pipeline.setContinuationCallableDepth(5);
    pipeline.setNumPayloads(5);
    pipeline.setNumAttributes(5);
    pipeline.enableMotionBlur();

    // OptixModuleをCUDAファイルから生成
    Module raygen_module, miss_module, hitgroups_module, textures_module, surfaces_module;
    raygen_module = pipeline.createModuleFromCudaFile(context, "cuda/raygen.cu");
    miss_module = pipeline.createModuleFromCudaFile(context, "cuda/miss.cu");
    hitgroups_module = pipeline.createModuleFromCudaFile(context, "cuda/hitgroups.cu");
    textures_module = pipeline.createModuleFromCudaFile(context, "cuda/textures.cu");
    surfaces_module = pipeline.createModuleFromCudaFile(context, "cuda/surfaces.cu");

    // レンダリング結果を保存する用のBitmapを用意
    const float width = pgGetWidth(), height = pgGetHeight();
    result_bitmap.allocate(Bitmap::Format::RGBA, width, height);
    accum_bitmap.allocate(FloatBitmap::Format::RGBA, width, height);

    // LaunchParamsの設定
    params.width = result_bitmap.width();
    params.height = result_bitmap.height();
    params.samples_per_launch = 1;
    params.max_depth = 5;
    params.white = 5.0f;

    initResultBufferOnDevice();

    // カメラの設定
    camera.setOrigin(make_float3(478.0f, 278.0f, -600.0f));
    camera.setLookat(make_float3(278.0f, 278.0f, 0.0f));
    camera.setUp(make_float3(0.0f, 1.0f, 0.0f));
    camera.setFarClip(5000);
    camera.setFov(40.0f);
    float3 U, V, W;
    camera.UVWFrame(U, V, W);
    camera.enableTracking(pgGetCurrentWindow());

    // Raygenプログラム
    ProgramGroup raygen_prg = pipeline.createRaygenProgram(context, raygen_module, "__raygen__pinhole");
    // Raygenプログラム用のShader Binding Tableデータ
    RaygenRecord raygen_record;
    raygen_prg.recordPackHeader(&raygen_record);
    raygen_record.data.camera =
    {
        .origin = camera.origin(),
        .lookat = camera.lookat(),
        .U = U,
        .V = V,
        .W = W,
        .farclip = camera.farClip()
    };
    sbt.setRaygenRecord(raygen_record);

    // Callable関数とShader Binding TableにCallable関数用のデータを登録するLambda関数
    auto setupCallable = [&](const Module& module, const std::string& dc, const std::string& cc)
    {
        EmptyRecord callable_record = {};
        auto [prg, id] = pipeline.createCallablesProgram(context, module, dc, cc);
        prg.recordPackHeader(&callable_record);
        sbt.addCallablesRecord(callable_record);
        return id;
    };

    // テクスチャ用のCallableプログラム
    uint32_t constant_prg_id = setupCallable(textures_module, DC_FUNC_STR("constant"), "");
    uint32_t bitmap_prg_id = setupCallable(textures_module, DC_FUNC_STR("bitmap"), "");

    // Surface用のCallableプログラム 
    // Diffuse
    uint32_t diffuse_sample_bsdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("sample_diffuse"), CC_FUNC_STR("bsdf_diffuse"));
    uint32_t diffuse_pdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("pdf_diffuse"), "");
    // Dielectric
    uint32_t dielectric_sample_bsdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("sample_dielectric"), CC_FUNC_STR("bsdf_dielectric"));
    uint32_t dielectric_pdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("pdf_dielectric"), "");
    // Disney
    uint32_t disney_sample_bsdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("sample_disney"), CC_FUNC_STR("bsdf_disney"));
    uint32_t disney_pdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("pdf_disney"), "");
    // Isotropic
    uint32_t isotropic_sample_bsdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("sample_isotropic"), CC_FUNC_STR("bsdf_isotropic"));
    uint32_t isotropic_pdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("pdf_isotropic"), "");
    // AreaEmitter
    uint32_t area_emitter_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("area_emitter"), "");

    // Shape用のCallableプログラム(主に面光源サンプリング用)
    uint32_t plane_sample_pdf_prg_id = setupCallable(hitgroups_module, DC_FUNC_STR("rnd_sample_plane"), CC_FUNC_STR("pdf_plane"));

    // 環境マッピング (Sphere mapping) 用のテクスチャとデータ準備
    auto env_texture = make_shared<ConstantTexture>(make_float3(0.0f), constant_prg_id);
    env_texture->copyToDevice();
    env = EnvironmentEmitter{env_texture};
    env.copyToDevice();

    // Missプログラム
    ProgramGroup miss_prg = pipeline.createMissProgram(context, miss_module, MS_FUNC_STR("envmap"));
    // Missプログラム用のShader Binding Tableデータ
    MissRecord miss_record;
    miss_prg.recordPackHeader(&miss_record);
    miss_record.data.env_data = env.devicePtr();
    sbt.setMissRecord(miss_record);

    // Hitgroupプログラム
    // Plane
    auto plane_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("plane"), IS_FUNC_STR("plane"));
    // Sphere
    auto sphere_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("sphere"), IS_FUNC_STR("sphere"));
    // Box
    auto box_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("box"), IS_FUNC_STR("box"));
    // Sphere medium
    auto sphere_medium_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("sphere"), IS_FUNC_STR("sphere_medium"));
    // Box medium
    auto box_medium_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("box"), IS_FUNC_STR("box_medium"));
    // Triangle mesh
    auto mesh_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("mesh"));

    struct Primitive
    {
        shared_ptr<Shape> shape;
        shared_ptr<Material> material;
        uint32_t sample_bsdf_id;
        uint32_t pdf_id;
    };

    uint32_t sbt_idx = 0;
    uint32_t sbt_offset = 0;
    uint32_t instance_id = 0;
    // ShapeとMaterialのデータをGPU上に準備しHitgroup用のSBTデータを追加するLambda関数
    auto setupPrimitive = [&](ProgramGroup& prg, const Primitive& primitive, const Matrix4f& transform)
    {
        // データをGPU側に用意
        primitive.shape->copyToDevice();
        primitive.material->copyToDevice();

        // Shader Binding Table へのデータの登録
        HitgroupRecord record;
        prg.recordPackHeader(&record);
        record.data =
        {
            .shape_data = primitive.shape->devicePtr(),
            .surface_info =
            {
                .data = primitive.material->devicePtr(),
                .sample_id = primitive.sample_bsdf_id,
                .bsdf_id = primitive.sample_bsdf_id,
                .pdf_id = primitive.pdf_id,
                .type = primitive.material->surfaceType()
            }
        };

        sbt.addHitgroupRecord(record);

        // GASをビルドし、IASに追加
        ShapeInstance instance{ primitive.shape->type(), primitive.shape, transform };
        instance.allowCompaction();
        instance.buildAccel(context, stream);
        instance.setSBTOffset(sbt_offset);
        instance.setId(instance_id);

        scene_ias.addInstance(instance);

        instance_id++;
        sbt_offset += RtNextWeekSBT::NRay;
    };

    std::vector<AreaEmitterInfo> area_emitter_infos;
    // 面光源用のSBTデータを用意しグローバル情報としての光源情報を追加するLambda関数
    // 光源サンプリング時にCallable関数ではOptixInstanceに紐づいた行列情報を取得できないので
    // 行列情報をAreaEmitterInfoに一緒に設定しておく
    // ついでにShapeInstanceによって光源用のGASも追加
    auto setupAreaEmitter = [&](
        ProgramGroup& prg,
        shared_ptr<Shape> shape,
        AreaEmitter area, Matrix4f transform, 
        uint32_t sample_pdf_id
    )
    {
        // Plane or Sphereにキャスト可能かチェック
        Assert(dynamic_pointer_cast<Plane>(shape) || dynamic_pointer_cast<Sphere>(shape), "The shape of area emitter must be a plane or sphere.");
        
        shape->copyToDevice();
        area.copyToDevice();

        HitgroupRecord record;
        prg.recordPackHeader(&record);
        record.data = 
        {
            .shape_data = shape->devicePtr(), 
            .surface_info = 
            {
                .data = area.devicePtr(),
                .sample_id = sample_pdf_id,
                .bsdf_id = area_emitter_prg_id,
                .pdf_id = sample_pdf_id,
                .type = SurfaceType::AreaEmitter
            }
        };

        sbt.addHitgroupRecord(record);

        // GASをビルドし、IASに追加
        ShapeInstance instance{shape->type(), shape, transform};
        instance.allowCompaction();
        instance.buildAccel(context, stream);
        instance.setSBTOffset(sbt_offset);
        instance.setId(instance_id);

        scene_ias.addInstance(instance);

        instance_id++;
        sbt_offset += RtNextWeekSBT::NRay;

        AreaEmitterInfo area_emitter_info = 
        {
            .shape_data = shape->devicePtr(), 
            .objToWorld = transform,
            .worldToObj = transform.inverse(), 
            .sample_id = sample_pdf_id, 
            .pdf_id = sample_pdf_id,
            .gas_handle = instance.handle()
        };
        area_emitter_infos.push_back(area_emitter_info);
    };

    // Scene ==========================================================================
    unsigned int seed = tea<4>(0, 0);

    // Ground boxes
    {
        auto ground_green = make_shared<ConstantTexture>(make_float3(0.48f, 0.83f, 0.53f), constant_prg_id);
        shared_ptr<ShapeGroup<Box, ShapeType::Custom>> ground_boxes = make_shared<ShapeGroup<Box, ShapeType::Custom>>();

        const int boxes_per_side = 20;
        for (int i = 0; i < boxes_per_side; i++)
        {
            for (int j = 0; j < boxes_per_side; j++)
            {
                float w = 100.0f;
                float x0 = -1000.0f + i * w;
                float z0 = -1000.0f + j * w;
                float y0 = 0.0f;
                float x1 = x0 + w;
                float y1 = rnd(seed, 1.0f, 101.0f);
                float z1 = z0 + w;

                Box box{ make_float3(x0, y0, z0), make_float3(x1, y1, z1) };
                box.setSbtIndex(sbt_idx);
                ground_boxes->addShape(box);
            }
        }
        auto diffuse = make_shared<Diffuse>(ground_green);
        auto transform = Matrix4f::identity();
        Primitive ground{ ground_boxes, diffuse, diffuse_sample_bsdf_prg_id, diffuse_pdf_prg_id };
        setupPrimitive(box_prg, ground, transform);
        sbt_idx++;
    }

    // Crowded sphere
    {
        auto spheres = make_shared<ShapeGroup<Sphere, ShapeType::Custom>>();
        int ns = 1000;
        for (int j = 0; j < ns; j++)
        {
            float3 rnd_pos = make_float3(rnd(seed), rnd(seed), rnd(seed)) * 165.0f;
            Sphere sphere = Sphere(rnd_pos, 10.0f);
            sphere.setSbtIndex(sbt_idx);
            spheres->addShape(Sphere(rnd_pos, 10.0f));
        }
        sbt_idx++;
        auto white = make_shared<ConstantTexture>(make_float3(0.73f), constant_prg_id);
        auto diffuse = make_shared<Diffuse>(white);
        auto transform = Matrix4f::translate({ -100.0f, 270.0f, 395.0f });
        Primitive crowded_sphere{ spheres, diffuse, diffuse_sample_bsdf_prg_id, diffuse_pdf_prg_id };
        setupPrimitive(sphere_prg, crowded_sphere, transform);
    }

    // Light
    {
        auto plane = make_shared<Plane>(make_float2(123.0f, 147.0f), make_float2(423.0f, 412.0f));
        plane->setSbtIndex(sbt_idx);
        sbt_idx++;
        auto white = make_shared<ConstantTexture>(make_float3(1.0f), constant_prg_id);
        AreaEmitter light{ white, 7.0f };
        auto transform = Matrix4f::translate({0.0f, 554.0f, 0.0f});
        setupAreaEmitter(plane_prg, plane, light, transform, plane_sample_pdf_prg_id);
    }

    // Glass sphere
    {
        auto sphere = make_shared<Sphere>(make_float3(260.0f, 150.0f, 45.0f), 50.0f);
        sphere->setSbtIndex(sbt_idx);
        sbt_idx++;
        auto white = make_shared<ConstantTexture>(make_float3(1.0f), constant_prg_id);
        auto glass = make_shared<Dielectric>(white, 1.5f);
        auto transform = Matrix4f::identity();
        Primitive glass_sphere{sphere, glass, dielectric_sample_bsdf_prg_id, dielectric_pdf_prg_id};
        setupPrimitive(sphere_prg, glass_sphere, transform);
    }

    // Metal sphere
    {
        auto sphere = make_shared<Sphere>(make_float3(0.0f, 150.0f, 145.0f), 50.0f);
        sphere->setSbtIndex(sbt_idx);
        sbt_idx++;
        auto silver = make_shared<ConstantTexture>(make_float3(0.8f, 0.8f, 0.9f), constant_prg_id);
        auto metal = make_shared<Disney>(silver);
        metal->setRoughness(0.5f);
        metal->setMetallic(1.0f);
        metal->setSubsurface(0.0f);
        auto transform = Matrix4f::identity();
        Primitive glass_sphere{ sphere, metal, disney_sample_bsdf_prg_id, disney_pdf_prg_id };
        setupPrimitive(sphere_prg, glass_sphere, transform);
    }

    // Sphere with motion blur
    {
        auto sphere = make_shared<Sphere>(make_float3(0.0f), 50.0f);
        sphere->setSbtIndex(sbt_idx);
        sphere->copyToDevice();
        sbt_idx++;
        auto orange = make_shared<ConstantTexture>(make_float3(0.7f, 0.3f, 0.1f), constant_prg_id);
        auto diffuse = make_shared<Diffuse>(orange);
        diffuse->copyToDevice();
        auto matrix_transform = Transform{ TransformType::MatrixMotion };

        HitgroupRecord record;
        sphere_prg.recordPackHeader(&record);
        record.data =
        {
            .shape_data = sphere->devicePtr(),
            .surface_info =
            {
                .data = diffuse->devicePtr(),
                .sample_id = diffuse_sample_bsdf_prg_id,
                .bsdf_id = diffuse_sample_bsdf_prg_id,
                .pdf_id = diffuse_pdf_prg_id,
                .type = diffuse->surfaceType()
            }
        };

        sbt.addHitgroupRecord(record);

        // 球体用のGASを用意
        GeometryAccel sphere_gas{ ShapeType::Custom };
        sphere_gas.addShape(sphere);
        sphere_gas.allowCompaction();
        sphere_gas.build(context, stream);

        // Motion blur用の開始と終了点における変換行列を用意
        float3 center1 = make_float3(400.0f, 400.0f, 200.0f);
        float3 center2 = center1 + make_float3(30.0f, 0.0f, 0.0f);
        Matrix4f begin_matrix = Matrix4f::translate(center1);
        Matrix4f end_matrix = Matrix4f::translate(center2);

        // Matrix motion 用のTransformを用意
        matrix_transform = Transform{ TransformType::MatrixMotion };
        matrix_transform.setChildHandle(sphere_gas.handle());
        matrix_transform.setMotionOptions(scene_ias.motionOptions());
        matrix_transform.setMatrixMotionTransform(begin_matrix, end_matrix);
        matrix_transform.copyToDevice();
        // childHandleからTransformのTraversableHandleを生成
        matrix_transform.buildHandle(context);

        // Instanceの生成
        Instance instance;
        instance.setSBTOffset(sbt_offset);
        instance.setId(instance_id);
        instance.setTraversableHandle(matrix_transform.handle());

        instance_id++;
        sbt_offset += RtNextWeekSBT::NRay;

        scene_ias.addInstance(instance);
    }

    // Earth sphere
    {
        auto sphere = make_shared<Sphere>(make_float3(400.0f, 200.0f, 400.0f), 100.0f);
        sphere->setSbtIndex(sbt_idx);
        sbt_idx++;
        auto earth_texture = make_shared<BitmapTexture>("resources/image/earth.jpg", bitmap_prg_id);
        auto diffuse = make_shared<Diffuse>(earth_texture);
        auto transform = Matrix4f::identity();
        Primitive earth_sphere{sphere, diffuse, diffuse_sample_bsdf_prg_id, diffuse_pdf_prg_id};
        setupPrimitive(sphere_prg, earth_sphere, transform);
    }

    // Sphere with dense medium
    {
        // Boundary sphere
        auto sphere = make_shared<Sphere>(make_float3(360.0f, 150.0f, 145.0f), 70.0f);
        sphere->setSbtIndex(sbt_idx);
        sbt_idx++;
        auto white = make_shared<ConstantTexture>(make_float3(1.0f), constant_prg_id);
        auto glass = make_shared<Dielectric>(white, 1.5f);
        Primitive boundary{ sphere, glass, dielectric_sample_bsdf_prg_id, dielectric_pdf_prg_id };
        setupPrimitive(sphere_prg, boundary, Matrix4f::identity());

        // Constant medium that fills inside boundary sphere
        auto medium = make_shared<SphereMedium>(make_float3(360.0f, 150.0f, 145.0f), 70.0f, 0.2f);
        medium->setSbtIndex(sbt_idx);
        sbt_idx++;
        auto isotropic = make_shared<Isotropic>(make_float3(0.2f, 0.4f, 0.9f));
        Primitive sphere_medium{ medium, isotropic, isotropic_sample_bsdf_prg_id, isotropic_pdf_prg_id };
        setupPrimitive(sphere_medium_prg, sphere_medium, Matrix4f::identity());
    }

    // Noise sphere
    {
        auto sphere = make_shared<Sphere>(make_float3(220.0f, 280.0f, 300.0f), 80.0f);
        sphere->setSbtIndex(sbt_idx);
        sbt_idx++;
        // @todo ConstantTexture -> NoiseTexture
        auto noise = make_shared<ConstantTexture>(make_float3(0.73f), constant_prg_id);
        auto diffuse = make_shared<Diffuse>(noise);
        auto transform = Matrix4f::identity();
        Primitive earth_sphere{ sphere, diffuse, diffuse_sample_bsdf_prg_id, diffuse_pdf_prg_id };
        setupPrimitive(sphere_prg, earth_sphere, transform);
    }

    // Sparse medium surrounds whole scene
    {
        auto medium = make_shared<BoxMedium>(make_float3(-5000.0f), make_float3(5000.0f), 0.0001f);
        medium->setSbtIndex(sbt_idx);
        sbt_idx++;
        auto isotropic = make_shared<Isotropic>(make_float3(1.0f));
        Primitive atomosphere{ medium, isotropic, isotropic_sample_bsdf_prg_id, isotropic_pdf_prg_id };
        setupPrimitive(box_medium_prg, atomosphere, Matrix4f::identity());
    }

    // 光源データをGPU側にコピー
    CUDABuffer<AreaEmitterInfo> d_area_emitter_infos;
    d_area_emitter_infos.copyToDevice(area_emitter_infos);
    params.lights = d_area_emitter_infos.deviceData();
    params.num_lights = static_cast<int>(area_emitter_infos.size());

    CUDA_CHECK(cudaStreamCreate(&stream));
    scene_ias.build(context, stream);
    sbt.createOnDevice();
    params.handle = scene_ias.handle();
    pipeline.create(context);
    d_params.allocate(sizeof(LaunchParams));

    // GUI setting
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    const char* glsl_version = "#version 150";
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(pgGetCurrentWindow()->windowPtr(), true);
    ImGui_ImplOpenGL3_Init(glsl_version);
}

// ----------------------------------------------------------------
void App::update()
{
    handleCameraUpdate();

    params.subframe_index++;
    d_params.copyToDeviceAsync(&params, sizeof(LaunchParams), stream);

    // OptiX レイトレーシングカーネルの起動
    optixLaunch(
        static_cast<OptixPipeline>(pipeline),
        stream,
        d_params.devicePtr(),
        sizeof(LaunchParams),
        &sbt.sbt(),
        params.width,
        params.height,
        1
    );

    CUDA_CHECK(cudaStreamSynchronize(stream));
    CUDA_SYNC_CHECK();

    // レンダリング結果をデバイスから取ってくる
    result_bitmap.copyFromDevice();
}

// ----------------------------------------------------------------
void App::draw()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Ray Tracing Next Week");

    ImGui::SliderFloat("White", &params.white, 1.0f, 1000.0f);
    ImGui::Text("Camera info:");
    ImGui::Text("Origin: %f %f %f", camera.origin().x, camera.origin().y, camera.origin().z);
    ImGui::Text("Lookat: %f %f %f", camera.lookat().x, camera.lookat().y, camera.lookat().z);
    ImGui::Text("Up: %f %f %f", camera.up().x, camera.up().y, camera.up().z);

    float farclip = camera.farClip();
    ImGui::SliderFloat("far clip", &farclip, 500.0f, 10000.0f);
    if (farclip != camera.farClip()) {
        camera.setFarClip(farclip);
        camera_update = true;
    }

    ImGui::Text("Frame rate: %.3f ms/frame (%.2f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::Text("Subframe index: %d", params.subframe_index);

    ImGui::End();

    ImGui::Render();

    result_bitmap.draw(0, 0);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (params.subframe_index == 4096)
        result_bitmap.write(pathJoin(pgAppDir(), "rtNextWeek.jpg"));
}

// ----------------------------------------------------------------
void App::close()
{
    env.free();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

// ----------------------------------------------------------------
void App::mouseDragged(float x, float y, int button)
{
    camera_update = true;
}

// ----------------------------------------------------------------
void App::mouseScrolled(float xoffset, float yoffset)
{
    camera_update = true;
}