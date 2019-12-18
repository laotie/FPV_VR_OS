//
// Created by Constantin on 29.03.2018.
//

#include "GLRStereoDaydream.h"
#include "jni.h"
#include <GLES2/gl2.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLProgramTexture.h>
#include <glm/gtx/matrix_decompose.hpp>
#include <memory>
#include <Helper/GLBufferHelper.hpp>
#include <MatrixHelper.h>
#include <gvr_util/util.h>
#include "CPUPriorities.hpp"

#include "vr/gvr/capi/include/gvr.h"
#include "vr/gvr/capi/include/gvr_types.h"

#include "Helper/GLHelper.hpp"

#define TAG "GLRendererDaydream"

GLRStereoDaydream::GLRStereoDaydream(JNIEnv* env,jobject androidContext,TelemetryReceiver& telemetryReceiver,gvr_context *gvr_context,int videoSurfaceID,int screenWidthP,int screenHeightP):
        mTelemetryReceiver(telemetryReceiver),
        mSettingsVR(env,androidContext,nullptr,gvr_context),
        mMatricesM(mSettingsVR),
        mFPSCalculator("OpenGL FPS",2000),
        distortionManager(DistortionManager::RADIAL_2),
        screenWidthP(screenWidthP),screenHeightP(screenHeightP){
    gvr_api_=gvr::GvrApi::WrapNonOwned(gvr_context);
    this->videoSurfaceID=videoSurfaceID;
    //----------
    buffer_viewports = gvr_api_->CreateEmptyBufferViewportList();
    recommended_buffer_viewports = gvr_api_->CreateEmptyBufferViewportList();
    scratch_viewport = gvr_api_->CreateBufferViewport();
}

void GLRStereoDaydream::placeGLElements() {
    float videoW=10;
    float videoH=videoW*1.0f/1.7777f;
    float videoX=-videoW/2.0f;
    float videoY=-videoH/2.0f;
    videoZ=-videoW/2.0f/(glm::tan(glm::radians(45.0f))*1.6f);
    videoZ*=1.1f;
    videoZ*=2;
    mOSDRenderer->placeGLElementsStereo(IPositionable::Rect2D(videoX,videoY,videoZ,videoW,videoH));
    mVideoRenderer->setWorldPosition(videoX,videoY,videoZ,videoW,videoH);
}

void GLRStereoDaydream::updateBufferViewports() {
    Matrices& t=mMatricesM.getWorldMatrices();
    //recommended_buffer_viewports.SetToRecommendedBufferViewports();
    //First the view ports for the video, handled by the async reprojection
    /*for(size_t eye=0;eye<2;eye++){
        recommended_buffer_viewports.GetBufferViewport(eye, &scratch_viewport);
        //scratch_viewport.SetSourceBufferIndex(GVR_BUFFER_INDEX_EXTERNAL_SURFACE);
        //scratch_viewport.SetExternalSurfaceId(videoSurfaceID);
        //scratch_viewport.SetExternalSurfaceId(GVR_EXTERNAL_SURFACE_ID_NONE);

        glm::mat4x4 glmM=glm::mat4x4(1);
        glmM=glm::scale(glmM,glm::vec3(1.6f,0.9f,1.0f));
        glmM=glm::translate(glmM,glm::vec3(0.0f,0.0f,-4));

        gvr::ClockTimePoint target_time = gvr::GvrApi::GetTimePointNow();
        target_time.monotonic_system_time_nanos+=10*1000*1000;
        glm::mat4x4 view=toGLM(gvr_api_->GetHeadSpaceFromStartSpaceRotation(target_time));
        glmM*=(eye==0 ? t.leftEyeView : t.rightEyeView);
        gvr::Mat4f gvrM=toGVR(glmM);
        scratch_viewport.SetTransform(gvrM);
        gvr::Rectf fov={50,50,30,30};
        scratch_viewport.SetSourceFov(fov);
        //auto b=scratch_viewport.GetSourceUv();
        //LOGDX("%f %f",b.bottom,b.top);
        //LOGDX("%f %f",b.left,b.right);
        gvr::Rectf uv={0,1,0,1}; //sample the same, full frame for left / right eye (video is not stereo)
        scratch_viewport.SetSourceUv(uv);
        scratch_viewport.SetReprojection(GVR_REPROJECTION_NONE);
        buffer_viewports.SetBufferViewport(eye,scratch_viewport);
    }*/
    recommended_buffer_viewports.SetToRecommendedBufferViewports();
    //
    for(size_t eye=0;eye<2;eye++){
        recommended_buffer_viewports.GetBufferViewport(eye, &scratch_viewport);
        //gvr::Rectf fov={45,45,45,45};
        //gvr::Rectf fov={33.15,33.15,33.15,33.15};
        //scratch_viewport.SetSourceFov(fov);
        //scratch_viewport.Set
        //scratch_viewport.SetExternalSurfaceId(videoSurfaceID);
        //scratch_viewport.SetSourceBufferIndex(GVR_BUFFER_INDEX_EXTERNAL_SURFACE);
        //scratch_viewport.SetReprojection(GVR_REPROJECTION_NONE);
        //scratch_viewport.SetTransform()
        buffer_viewports.SetBufferViewport(eye,scratch_viewport);
    }
    //
    for(size_t eye=0;eye<2;eye++){
        recommended_buffer_viewports.GetBufferViewport(eye, &scratch_viewport);
        //gvr::Rectf fov={45,45,45,45};
        //gvr::Rectf fov={33.15,33.15,33.15,33.15};
        //scratch_viewport.SetSourceFov(fov);
        scratch_viewport.SetReprojection(GVR_REPROJECTION_FULL);
        buffer_viewports.SetBufferViewport(eye+2,scratch_viewport);
    }
}

void GLRStereoDaydream::onSurfaceCreated(JNIEnv * env,jobject androidContext,jint optionalVideoTexture) {
    gvr_api_->InitializeGl();
    std::vector<gvr::BufferSpec> specs;
    specs.push_back(gvr_api_->CreateBufferSpec());
    framebuffer_size = gvr_api_->GetMaximumEffectiveRenderTargetSize();
    specs[0].SetSize(framebuffer_size);
    specs[0].SetColorFormat(GVR_COLOR_FORMAT_RGBA_8888);
    specs[0].SetDepthStencilFormat(GVR_DEPTH_STENCIL_FORMAT_DEPTH_16);
    swap_chain = std::make_unique<gvr::SwapChain>(gvr_api_->CreateSwapChain(specs));

    mBasicGLPrograms=std::make_unique<BasicGLPrograms>(&distortionManager);
    mOSDRenderer=std::make_unique<OSDRenderer>(env,androidContext,*mBasicGLPrograms,mTelemetryReceiver);
    mBasicGLPrograms->text.loadTextRenderingData(env,androidContext,mOSDRenderer->settingsOSDStyle.OSD_TEXT_FONT_TYPE);

    mVideoRenderer=std::make_unique<VideoRenderer>(VideoRenderer::VIDEO_RENDERING_MODE::RM_NORMAL,0,mBasicGLPrograms->vc);

    mMatricesM.calculateProjectionAndDefaultView(headset_fovY_full, framebuffer_size.width / 2.0f /
                                                                    framebuffer_size.height);
    placeGLElements();
    //
    float tesselatedRectSize=2.5; //6.2f
    const float offsetY=0.0f;
    auto tmp=ColoredGeometry::makeTesselatedColoredRectLines(LINE_MESH_TESSELATION_FACTOR,{-tesselatedRectSize/2.0f,-tesselatedRectSize/2.0f+offsetY,-2},tesselatedRectSize,tesselatedRectSize,Color::BLUE);
    nColoredVertices=GLBufferHelper::createAllocateGLBufferStatic(glBufferVC,tmp);
    tmp=ColoredGeometry::makeTesselatedColoredRectLines(LINE_MESH_TESSELATION_FACTOR,{-tesselatedRectSize/2.0f,-tesselatedRectSize/2.0f+offsetY,-2},tesselatedRectSize,tesselatedRectSize,Color::GREEN);
    GLBufferHelper::createAllocateGLBufferStatic(glBufferVCX,tmp);
}


void GLRStereoDaydream::onSurfaceChanged(int width, int height) {
    //In GVR btw Daydream mode the onSurfaceChanged loses its importance since
    //we are rendering the scene into an off-screen buffer anyways
}

void GLRStereoDaydream::onDrawFrame() {
    //Calculate & print fps
    mFPSCalculator.tick();
    //LOGD("FPS: %f",mFPSCalculator.getCurrentFPS());

    mMatricesM.calculateNewHeadPoseIfNeeded(gvr_api_.get(), 16);
    Matrices& worldMatrices=mMatricesM.getWorldMatrices();

    updateBufferViewports();

    /*gvr::Frame frame = swap_chain->AcquireFrame();
    frame.BindBuffer(0); //0 is the 0 from createSwapChain()

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    //Video is handled by the async reprojection surface
    distortionManager.updateDistortionWithIdentity();
    for(uint32_t eye=0;eye<2;eye++){
        drawEyeOSD(eye, worldMatrices);
    }
    frame.Unbind();
    frame.Submit(buffer_viewports, worldMatrices.lastHeadPos);

    //
    //glClearColor(0.3f, 0.0f, 0.0f, 0.0f);
    //glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);*/

    distortionManager.updateDistortion(mInverse,MAX_RAD_SQ,screen_params,texture_params);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    for(int eye=0;eye<2;eye++){
        drawEyeOSDVDDC(eye);
    }

    GLHelper::checkGlError("GLRStereoDaydream::drawFrame");
}


void GLRStereoDaydream::drawEyeOSD(uint32_t eye, Matrices &worldMatrices) {
    buffer_viewports.GetBufferViewport(eye, &scratch_viewport);

    const gvr::Rectf& rect = scratch_viewport.GetSourceUv();
    int left = static_cast<int>(rect.left * framebuffer_size.width);
    int bottom = static_cast<int>(rect.bottom * framebuffer_size.width);
    int width = static_cast<int>((rect.right - rect.left) * framebuffer_size.width);
    int height = static_cast<int>((rect.top - rect.bottom) * framebuffer_size.height);
    glViewport(left, bottom, width, height);

    const gvr_rectf fov = scratch_viewport.GetSourceFov();
    const gvr::Mat4f perspective =ndk_hello_vr::PerspectiveMatrixFromView(fov, MIN_Z_DISTANCE, MAX_Z_DISTANCE);
    const auto eyeM=gvr_api_->GetEyeFromHeadMatrix(eye==0 ? GVR_LEFT_EYE : GVR_RIGHT_EYE);
    const auto rotM=gvr_api_->GetHeadSpaceFromStartSpaceRotation(gvr::GvrApi::GetTimePointNow());
    const auto viewM=toGLM(ndk_hello_vr::MatrixMul(eyeM,rotM));
    //const auto viewM=toGLM(eyeM);
    const auto projectionM=toGLM(perspective);
    if(eye==0){
        //mVideoRenderer->punchHole(leftEye,projection);
        mOSDRenderer->updateAndDrawElementsGL(viewM,projectionM);
    }else{
        //mVideoRenderer->punchHole(rightEye,projection);
        mOSDRenderer->drawElementsGL(viewM,projectionM);
    }
    glLineWidth(6.0f);
    /*mBasicGLPrograms->vc.beforeDraw(glBufferVC);
    mBasicGLPrograms->vc.draw(viewM,projectionM,0,nColoredVertices,GL_LINES);
    mBasicGLPrograms->vc.afterDraw();*/
}

void GLRStereoDaydream::drawEyeOSDVDDC(uint32_t eye) {
    const int ViewPortW=(int)(screenWidthP/2.0f);
    const int ViewPortH=(int)(screenHeightP);
    if(eye==0){
        glViewport(0,0,ViewPortW,ViewPortH);
    }else{
        glViewport(ViewPortW,0,ViewPortW,ViewPortH);
    }
    distortionManager.leftEye=eye==0;
    const auto rotM=toGLM(gvr_api_->GetHeadSpaceFromStartSpaceRotation(gvr::GvrApi::GetTimePointNow()));
    auto viewM=mViewM[eye]*rotM;
    auto projM=mProjectionM[eye];

    if(eye==0){
        mOSDRenderer->updateAndDrawElementsGL(viewM,projM);
    }else{
        mOSDRenderer->drawElementsGL(viewM,projM);
    }

    glLineWidth(3.0f);
    /*mBasicGLPrograms->vc.beforeDraw(glBufferVCX);
    mBasicGLPrograms->vc.draw(viewM,projM,0,nColoredVertices,GL_LINES);
    mBasicGLPrograms->vc.afterDraw();*/
}

void GLRStereoDaydream::updateHeadsetParams(const MDeviceParams& mDP) {
    LOGD("%s",MLensDistortion::MDeviceParamsAsString(mDP).c_str());
    auto polynomialRadialDistortion=MPolynomialRadialDistortion(mDP.radial_distortion_params);
    //auto polynomialRadialDistortion=MPolynomialRadialDistortion({0.441, 0.156});

    const auto GetYEyeOffsetMeters= MLensDistortion::GetYEyeOffsetMeters(mDP.vertical_alignment,
                                                                         mDP.tray_to_lens_distance,
                                                                         mDP.screen_height_meters);
    const auto fovLeft= MLensDistortion::CalculateFov(mDP.device_fov_left, GetYEyeOffsetMeters,
                                                      mDP.screen_to_lens_distance,
                                                      mDP.inter_lens_distance,
                                                      polynomialRadialDistortion,
                                                      mDP.screen_width_meters, mDP.screen_height_meters);
    const auto fovRight=MLensDistortion::reverseFOV(fovLeft);


    MLensDistortion::CalculateViewportParameters_NDC(0, GetYEyeOffsetMeters,
                                                     mDP.screen_to_lens_distance,
                                                     mDP.inter_lens_distance, fovLeft,
                                                     mDP.screen_width_meters, mDP.screen_height_meters,
                                                     &screen_params[0], &texture_params[0]);
    MLensDistortion::CalculateViewportParameters_NDC(1, GetYEyeOffsetMeters,
                                                     mDP.screen_to_lens_distance,
                                                     mDP.inter_lens_distance, fovRight,
                                                     mDP.screen_width_meters, mDP.screen_height_meters,
                                                     &screen_params[1], &texture_params[1]);


    MAX_RAD_SQ=1.0f;
    bool done=false;
    while(MAX_RAD_SQ<2.0f && !done){
        const auto& inverse=polynomialRadialDistortion.getApproximateInverseDistortion(MAX_RAD_SQ,DistortionManager::N_RADIAL_UNDISTORTION_COEFICIENTS);
        LOGD("Max Rad Sq%f",MAX_RAD_SQ);
        for(float r=0;r<MAX_RAD_SQ;r+=0.01f) {
            const float deviation = MPolynomialRadialDistortion::calculateDeviation(r,polynomialRadialDistortion,inverse);
            //LOGD("r %f | Deviation %f",r,deviation);
            if (deviation > 0.001f) {
                done = true;
                MAX_RAD_SQ-= 0.01f;
                break;
            }
        }
        MAX_RAD_SQ+=0.01f;
    }
    LOGD("Max Rad Sq%f",MAX_RAD_SQ);
    mInverse=polynomialRadialDistortion.getApproximateInverseDistortion(MAX_RAD_SQ,DistortionManager::N_RADIAL_UNDISTORTION_COEFICIENTS);

    distortionManager.updateDistortion(mInverse,MAX_RAD_SQ,screen_params,texture_params);
    distortionManager.updateDistortionWithIdentity();

    mProjectionM[0]=perspective(fovLeft,MIN_Z_DISTANCE,MAX_Z_DISTANCE);
    mProjectionM[1]=perspective(fovRight,MIN_Z_DISTANCE,MAX_Z_DISTANCE);
    const float inter_lens_distance=mDP.inter_lens_distance;
    mViewM[0]=glm::translate(glm::mat4(1.0f),glm::vec3(inter_lens_distance*0.5f,0,0));
    mViewM[1]=glm::translate(glm::mat4(1.0f),glm::vec3(-inter_lens_distance*0.5f,0,0));
}



//----------------------------------------------------JAVA bindings---------------------------------------------------------------

#define JNI_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_constantin_fpv_1vr_PlayStereo_GLRStereoDaydream_##method_name

inline jlong jptr(GLRStereoDaydream *glRenderer) {
    return reinterpret_cast<intptr_t>(glRenderer);
}
inline GLRStereoDaydream *native(jlong ptr) {
    return reinterpret_cast<GLRStereoDaydream *>(ptr);
}

extern "C" {

JNI_METHOD(jlong, nativeConstruct)
(JNIEnv *env, jobject instance,jobject androidContext,jlong telemetryReceiver, jlong native_gvr_api,jint videoSurfaceID,jint screenWidthP,jint screenHeightP) {
return jptr(new GLRStereoDaydream(env,androidContext,*reinterpret_cast<TelemetryReceiver*>(telemetryReceiver),reinterpret_cast<gvr_context *>(native_gvr_api),
        (int)videoSurfaceID,(int)screenWidthP,(int)screenHeightP));
}

JNI_METHOD(void, nativeDelete)
        (JNIEnv *env, jobject instance, jlong glRenderer) {
    delete native(glRenderer);
}

JNI_METHOD(void, nativeOnSurfaceCreated)
        (JNIEnv *env, jobject instance, jlong glRenderer,jfloat fovY_full,jfloat ipd_full,jobject androidContext) {
    native(glRenderer)->OnSurfaceCreated(env,androidContext,0);
}

JNI_METHOD(void, nativeOnSurfaceChanged)
        (JNIEnv *env, jobject obj, jlong glRendererStereo,jint w,jint h) {
    native(glRendererStereo)->OnSurfaceChanged(w, h);
}

JNI_METHOD(void, nativeOnDrawFrame)
        (JNIEnv *env, jobject obj, jlong glRenderer) {
    native(glRenderer)->OnDrawFrame();
}


JNI_METHOD(void, nativeUpdateHeadsetParams)
(JNIEnv *env, jobject obj, jlong glRendererStereo,
 jfloat screen_width_meters,
 jfloat screen_height_meters,
 jfloat screen_to_lens_distance,
 jfloat inter_lens_distance,
 jint vertical_alignment,
 jfloat tray_to_lens_distance,
 jfloatArray device_fov_left,
 jfloatArray radial_distortion_params
) {
    std::array<float,4> device_fov_left1{};
    std::vector<float> radial_distortion_params1(2);

    jfloat *arrayP=env->GetFloatArrayElements(device_fov_left, nullptr);
    std::memcpy(device_fov_left1.data(),&arrayP[0],4*sizeof(float));
    env->ReleaseFloatArrayElements(device_fov_left,arrayP,0);
    arrayP=env->GetFloatArrayElements(radial_distortion_params, nullptr);
    std::memcpy(radial_distortion_params1.data(),&arrayP[0],2*sizeof(float));
    env->ReleaseFloatArrayElements(radial_distortion_params,arrayP,0);

    const MDeviceParams deviceParams{screen_width_meters,screen_height_meters,screen_to_lens_distance,inter_lens_distance,vertical_alignment,tray_to_lens_distance,
                                     device_fov_left1,radial_distortion_params1};

    native(glRendererStereo)->updateHeadsetParams(deviceParams);
}

}