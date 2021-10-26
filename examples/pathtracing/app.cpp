#include "app.h"

void App::initResultBufferOnDevice()
{
    params.subframe_index = 0;

    result_bitmap.allocateDevicePtr();
    accum_bitmap.allocateDevicePtr();
    normal_bitmap.allocateDevicePtr();
    albedo_bitmap.allocateDevicePtr();
    depth_bitmap.allocateDevicePtr();

    params.result_buffer = reinterpret_cast<uchar4*>(result_bitmap.devicePtr());
    params.accum_buffer = reinterpret_cast<float4*>(accum_bitmap.devicePtr());
    params.normal_buffer = reinterpret_cast<float3*>(normal_bitmap.devicePtr());
    params.albedo_buffer = reinterpret_cast<float3*>(albedo_bitmap.devicePtr());
    params.depth_buffer = reinterpret_cast<float*>(depth_bitmap.devicePtr());

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
    context.disableValidation();
    context.create();

    // Instance acceleration structureの初期化
    scene_ias = InstanceAccel{InstanceAccel::Type::Instances};

    // パイプラインの設定
    pipeline.setLaunchVariableName("params");
    pipeline.setDirectCallableDepth(5);
    pipeline.setContinuationCallableDepth(5);
    pipeline.setNumPayloads(5);
    pipeline.setNumAttributes(5);

    // OptixModuleをCUDAファイルから生成
    Module raygen_module, miss_module, hitgroups_module, textures_module, surfaces_module;
    raygen_module = pipeline.createModuleFromCudaFile(context, "cuda/raygen.cu");
    miss_module = pipeline.createModuleFromCudaFile(context, "cuda/miss.cu");
    hitgroups_module = pipeline.createModuleFromCudaFile(context, "cuda/hitgroups.cu");
    textures_module = pipeline.createModuleFromCudaFile(context, "cuda/textures.cu");
    surfaces_module = pipeline.createModuleFromCudaFile(context, "cuda/surfaces.cu");

    // レンダリング結果を保存する用のBitmapを用意
    result_bitmap.allocate(Bitmap::Format::RGBA, pgGetWidth(), pgGetHeight());
    accum_bitmap.allocate(FloatBitmap::Format::RGBA, pgGetWidth(), pgGetHeight());
    normal_bitmap.allocate(FloatBitmap::Format::RGB, pgGetWidth(), pgGetHeight());
    albedo_bitmap.allocate(FloatBitmap::Format::RGB, pgGetWidth(), pgGetHeight());
    depth_bitmap.allocate(FloatBitmap::Format::GRAY, pgGetWidth(), pgGetHeight());

    // LaunchParamsの設定
    params.width = result_bitmap.width();
    params.height = result_bitmap.height();
    params.samples_per_launch = 1;
    params.max_depth = 10;
    params.white = 1.0f;

    initResultBufferOnDevice();

    // カメラの設定
    camera.setOrigin(make_float3(-333.0f, 80.0f, -800.0f));
    camera.setLookat(make_float3(0.0f, -225.0f, 0.0f));
    camera.setUp(make_float3(0.0f, 1.0f, 0.0f));
    camera.setFarClip(5000);
    camera.setFov(40.0f);
    camera.setAspect(static_cast<float>(params.width) / params.height);
    camera.enableTracking(pgGetCurrentWindow());
    float3 U, V, W;
    camera.UVWFrame(U, V, W);

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
    uint32_t checker_prg_id = setupCallable(textures_module, DC_FUNC_STR("checker"), "");
    uint32_t bitmap_prg_id = setupCallable(textures_module, DC_FUNC_STR("bitmap"), "");

    // Surface用のCallableプログラム 
    // Diffuse
    uint32_t diffuse_sample_bsdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("sample_diffuse"), CC_FUNC_STR("bsdf_diffuse"));
    uint32_t diffuse_pdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("pdf_diffuse"), "");
    // Conductor
    uint32_t conductor_sample_bsdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("sample_conductor"), CC_FUNC_STR("bsdf_conductor"));
    uint32_t conductor_pdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("pdf_conductor"), "");
    // Dielectric
    uint32_t dielectric_sample_bsdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("sample_dielectric"), CC_FUNC_STR("bsdf_dielectric"));
    uint32_t dielectric_pdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("pdf_dielectric"), "");
    // Disney
    uint32_t disney_sample_bsdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("sample_disney"), CC_FUNC_STR("bsdf_disney"));
    uint32_t disney_pdf_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("pdf_disney"), "");
    // AreaEmitter
    uint32_t area_emitter_prg_id = setupCallable(surfaces_module, DC_FUNC_STR("area_emitter"), "");

    // Shape用のCallableプログラム(主に面光源サンプリング用)
    uint32_t plane_sample_pdf_prg_id = setupCallable(hitgroups_module, DC_FUNC_STR("rnd_sample_plane"), CC_FUNC_STR("pdf_plane"));
    uint32_t sphere_sample_pdf_prg_id = setupCallable(hitgroups_module, DC_FUNC_STR("rnd_sample_sphere"), CC_FUNC_STR("pdf_sphere"));

    // 環境マッピング (Sphere mapping) 用のテクスチャとデータ準備
    // 画像ファイルはリポジトリには含まれていないので、任意の画像データを設定してください
     auto env_texture = make_shared<FloatBitmapTexture>("resources/image/dikhololo_night_4k.exr", bitmap_prg_id); 
    //auto env_texture = make_shared<ConstantTexture>(make_float3(0.0f), constant_prg_id);
    env_texture->copyToDevice();
    env = EnvironmentEmitter{env_texture};
    env.copyToDevice();

    // Missプログラム
    ProgramGroup miss_prg = pipeline.createMissProgram(context, miss_module, MS_FUNC_STR("envmap"));
    // Missプログラム用のShader Binding Tableデータ
    MissRecord miss_record;
    miss_prg.recordPackHeader(&miss_record);
    miss_record.data.env_data = env.devicePtr();
    MissRecord miss_shadow_record;
    miss_prg.recordPackHeader(&miss_shadow_record);
    sbt.setMissRecord(miss_record, miss_shadow_record);

    // Hitgroupプログラム
    // Plane
    auto plane_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("plane"), IS_FUNC_STR("plane"));
    auto plane_shadow_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("shadow"), IS_FUNC_STR("plane"));
    // Sphere
    auto sphere_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("sphere"), IS_FUNC_STR("sphere"));
    auto sphere_shadow_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("shadow"), IS_FUNC_STR("sphere"));
    // Cylinder
    auto cylinder_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("cylinder"), IS_FUNC_STR("cylinder"));
    auto cylinder_shadow_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("shadow"), IS_FUNC_STR("cylinder"));
    // Triangle mesh
    auto mesh_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("mesh"));
    auto mesh_shadow_prg = pipeline.createHitgroupProgram(context, hitgroups_module, CH_FUNC_STR("shadow"));

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
    auto setupPrimitive = [&](ProgramGroup& prg, ProgramGroup& shadow_prg, const Primitive& primitive, const Matrix4f& transform)
    {
        // データをGPU側に用意
        primitive.shape->copyToDevice();
        primitive.shape->setSbtIndex(sbt_idx);
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

        HitgroupRecord shadow_record;
        shadow_prg.recordPackHeader(&shadow_record);

        sbt.addHitgroupRecord(record, shadow_record);
        sbt_idx++;

        // GASをビルドし、IASに追加
        ShapeInstance instance{primitive.shape->type(), primitive.shape, transform};
        instance.allowCompaction();
        instance.buildAccel(context, stream);
        instance.setSBTOffset(sbt_offset);
        instance.setId(instance_id);

        scene_ias.addInstance(instance);

        instance_id++;
        sbt_offset += PathTracingSBT::NRay;
    };

    vector<AreaEmitterInfo> area_emitter_infos;
    // 面光源用のSBTデータを用意するLambda関数
    auto setupAreaEmitter = [&](
        ProgramGroup& prg, ProgramGroup& shadow_prg, 
        shared_ptr<Shape> shape,
        AreaEmitter area, Matrix4f transform, 
        uint32_t sample_pdf_id
    )
    {        
        ASSERT(dynamic_pointer_cast<Plane>(shape) || dynamic_pointer_cast<Sphere>(shape), "The shape of area emitter must be a plane or sphere.");

        shape->copyToDevice();
        shape->setSbtIndex(sbt_idx);
        area.copyToDevice();

        HitgroupRecord record;
        prg.recordPackHeader(&record);
        record.data = 
        {
            .shape_data = shape->devicePtr(), 
            .surface_info = 
            {
                .data = area.devicePtr(),
                .sample_id = area_emitter_prg_id, // 使わない
                .bsdf_id = area_emitter_prg_id,
                .pdf_id = area_emitter_prg_id,    // 使わない
                .type = SurfaceType::AreaEmitter
            }
        };

        HitgroupRecord shadow_record = {};
        shadow_prg.recordPackHeader(&shadow_record);
        sbt_idx++;

        sbt.addHitgroupRecord(record, shadow_record);

        // GASをビルドし、IASに追加
        ShapeInstance instance{shape->type(), shape, transform};
        instance.allowCompaction();
        instance.buildAccel(context, stream);
        instance.setSBTOffset(sbt_offset);
        instance.setId(instance_id);

        scene_ias.addInstance(instance);

        instance_id++;
        sbt_offset += PathTracingSBT::NRay;

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
    // Bunny
    {
        // Shape
        auto bunny = make_shared<TriangleMesh>("resources/model/uv_bunny.obj");
        // Texture
        auto bunny_checker = make_shared<CheckerTexture>(make_float3(0.3f), make_float3(0.8f, 0.05f, 0.05f), 10, checker_prg_id);
        bunny_checker->copyToDevice();
        // Material
        auto bunny_disney = make_shared<Disney>(bunny_checker);
        bunny_disney->setRoughness(0.3f);
        bunny_disney->setMetallic(1.0f);
        // Transform
        Matrix4f transform = Matrix4f::translate({-50.0f, -272.0f, 300.0f}) * Matrix4f::rotate(math::pi, {0.0f, 1.0f, 0.0f}) * Matrix4f::scale(1200.0f);
        Primitive primitive{bunny, bunny_disney, disney_sample_bsdf_prg_id, disney_pdf_prg_id};
        setupPrimitive(mesh_prg, mesh_shadow_prg, primitive, transform);
    }
    
    // Armadillo
    {
        // Shape
        auto armadillo = make_shared<TriangleMesh>("resources/model/Armadillo.ply");
        armadillo->smooth();
        // Texture
        auto armadillo_constant = make_shared<ConstantTexture>(make_float3(1.0f), constant_prg_id);
        armadillo_constant->copyToDevice();
        // Material
        auto armadillo_conductor = make_shared<Conductor>(armadillo_constant);
        // Transform
        Matrix4f transform = Matrix4f::translate({250.0f, -210.0f, -150.0f}) * Matrix4f::scale(1.2f);
        Primitive primitive{armadillo, armadillo_conductor, conductor_sample_bsdf_prg_id, conductor_pdf_prg_id};
        setupPrimitive(mesh_prg, mesh_shadow_prg, primitive, transform);
    }

    // Teapot
    {
        // Shape
        auto teapot = make_shared<TriangleMesh>("resources/model/teapot.obj");
        // Texture
        auto teapot_constant = make_shared<ConstantTexture>(make_float3(0.325f, 0.702f, 0.709f), constant_prg_id);
        teapot_constant->copyToDevice();
        // Material
        auto teapot_diffuse = make_shared<Diffuse>(teapot_constant);
        // Transform
        Matrix4f transform = Matrix4f::translate({-250.0f, -275.0f, -150.0f}) * Matrix4f::scale(40.0f);
        Primitive primitive { teapot, teapot_diffuse, diffuse_sample_bsdf_prg_id, diffuse_pdf_prg_id };
        setupPrimitive(mesh_prg, mesh_shadow_prg, primitive, transform);
    }

    // Earth
    {
        // Shape
        auto earth_sphere = make_shared<Sphere>(make_float3(0.0f), 90.0f);
        // Texture
        auto earth_bitmap = make_shared<BitmapTexture>("resources/image/earth.jpg", bitmap_prg_id);
        earth_bitmap->copyToDevice();
        // Material
        auto earth_diffuse = make_shared<Diffuse>(earth_bitmap);
        // Transform
        Matrix4f transform = Matrix4f::translate({-250.0f, -185.0f, 150.0f});
        Primitive primitive { earth_sphere, earth_diffuse, diffuse_sample_bsdf_prg_id, diffuse_pdf_prg_id };
        setupPrimitive(sphere_prg, sphere_shadow_prg, primitive, transform);
    }

    // Glass sphere
    {
        // Shape
        auto glass_sphere = make_shared<Sphere>(make_float3(0.0f), 80.0f);
        // Texture
        auto white_constant = make_shared<ConstantTexture>(make_float3(1.0f), constant_prg_id);
        white_constant->copyToDevice();
        // Material
        auto glass = make_shared<Dielectric>(white_constant, 1.5f);
        // Transform 
        Matrix4f transform = Matrix4f::translate({250.0f, -195.0f, 150.0f}) * Matrix4f::rotate(math::pi, {0.0f, 1.0f, 0.0f});
        Primitive primitive { glass_sphere, glass, dielectric_sample_bsdf_prg_id, dielectric_pdf_prg_id };
        setupPrimitive(sphere_prg, sphere_shadow_prg, primitive, transform);
    }

    // Cylinder
    {
        // Shape
        auto cylinder = make_shared<Cylinder>(60.0f, 100.0f);
        // Texture
        auto cylinder_checker = make_shared<CheckerTexture>(make_float3(0.3f), make_float3(0.9f), 10, checker_prg_id);
        cylinder_checker->copyToDevice();
        // Material
        auto cylinder_diffuse = make_shared<Diffuse>(cylinder_checker);
        // Transform
        Matrix4f transform = Matrix4f::translate({0.0f, -220.0f, -300.0f});
        Primitive primitive { cylinder, cylinder_diffuse, diffuse_sample_bsdf_prg_id, diffuse_pdf_prg_id };
        setupPrimitive(cylinder_prg, cylinder_shadow_prg, primitive, transform);
    }

    // Ground
    {
        // Shape
        auto ground = make_shared<Plane>(make_float2(-500.0f, -500.0f), make_float2(500.0f, 500.0f));
        // Texture
        auto ground_texture = make_shared<ConstantTexture>(make_float3(0.25f), constant_prg_id);
        ground_texture->copyToDevice();
        // Material
        auto ground_diffuse = make_shared<Diffuse>(ground_texture);
        // Transform
        Matrix4f transform = Matrix4f::translate({0.0f, -275.0f, 0.0f});
        Primitive primitive { ground, ground_diffuse, diffuse_sample_bsdf_prg_id, diffuse_pdf_prg_id };
        setupPrimitive(plane_prg, plane_shadow_prg, primitive, transform);
    }

    // 面光源1 : Plane
    {
        // Shape
        auto plane_light = make_shared<Plane>();
        // Texture
        auto white = make_shared<ConstantTexture>(make_float3(1.0f), constant_prg_id);
        white->copyToDevice();
        // Area emitter
        auto plane_area_emitter = AreaEmitter(white, 50.0f);
        Matrix4f transform = Matrix4f::translate({200.0f, 50.0f, 200.0f}) * Matrix4f::rotate(math::pi / 4.0f, {0.5f, 0.5f, 0.2f}) * Matrix4f::scale(50.0f);
        setupAreaEmitter(plane_prg, plane_shadow_prg, plane_light, plane_area_emitter, transform, plane_sample_pdf_prg_id);
    }

    // 面光源2 : Sphere
    {
        // Shape
        auto sphere_light = make_shared<Sphere>();
        // Texture
        auto orange = make_shared<ConstantTexture>(make_float3(0.914f, 0.639f, 0.149f), constant_prg_id);
        orange->copyToDevice();
        // Area emitter
        auto sphere_area_emitter = AreaEmitter(orange, 50.0f);
        Matrix4f transform = Matrix4f::translate({-200.0f, 50.0f, -200.0f}) * Matrix4f::scale(30.0f);
        setupAreaEmitter(sphere_prg, sphere_shadow_prg, sphere_light, sphere_area_emitter, transform, sphere_sample_pdf_prg_id);
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

    d_params.copyToDeviceAsync(&params, sizeof(LaunchParams), stream);

    float start_time = pgGetElapsedTimef();

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
    params.subframe_index++;

    render_time = pgGetElapsedTimef() - start_time;

    CUDA_CHECK(cudaStreamSynchronize(stream));
    CUDA_SYNC_CHECK();

    // レンダリング結果をデバイスから取ってくる
    result_bitmap.copyFromDevice();
    normal_bitmap.copyFromDevice();
    albedo_bitmap.copyFromDevice();
    depth_bitmap.copyFromDevice();
}

// ----------------------------------------------------------------
void App::draw()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Path tracing GUI");

    ImGui::SliderFloat("White", &params.white, 0.01f, 1.0f);
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
    ImGui::Text("Render time: %.3f ms/frame", render_time * 1000.0f);
    ImGui::Text("Subframe index: %d", params.subframe_index);

    ImGui::End();

    ImGui::Render();

    result_bitmap.draw(0, 0, pgGetWidth() / 2, pgGetHeight() / 2);
    normal_bitmap.draw(pgGetWidth() / 2, 0, pgGetWidth() / 2, pgGetHeight() / 2);
    albedo_bitmap.draw(0, pgGetHeight() / 2, pgGetWidth() / 2, pgGetHeight() / 2);
    depth_bitmap.draw(pgGetWidth() / 2, pgGetHeight() / 2, pgGetWidth() / 2, pgGetHeight() / 2);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (params.subframe_index == 4096)
        result_bitmap.write(pathJoin(pgAppDir(), "pathtracing.jpg"));
}

// ----------------------------------------------------------------
void App::close()
{
    env.free();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    pipeline.destroy();
    context.destroy();
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