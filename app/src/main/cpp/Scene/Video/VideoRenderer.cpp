
//It is complete bullshit to call java code via ndk that is then calling ndk code again - but when building
//for pre-android 9, there is no other way around
#define FPV_VR_USE_JAVA_FOR_SURFACE_TEXTURE_UPDATE

#include <Color/Color.hpp>
#include <GeometryBuilder/ColoredGeometry.hpp>
#include <GeometryBuilder/TexturedGeometry.hpp>
#include "VideoRenderer.h"
#include "Helper/GLHelper.hpp"
#include "Helper/GLBufferHelper.hpp"

#ifndef FPV_VR_USE_JAVA_FOR_SURFACE_TEXTURE_UPDATE
#include <android/surface_texture.h>
#include <android/surface_texture_jni.h>
#endif


constexpr auto TAG="VideoRenderer";
#define LOGD1(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

VideoRenderer::VideoRenderer(VIDEO_RENDERING_MODE mode,const GLuint videoTexture,const GLProgramVC& glRenderGeometry,GLProgramTexture *glRenderTexEx,GLProgramSpherical *glPSpherical,float sphereRadius):
mSphere(sphereRadius,36*1,18*1),mVideoTexture(videoTexture),
mMode(mode),mPositionDebug(glRenderGeometry,6, false),mGLRenderGeometry(glRenderGeometry){
    mGLRenderTexEx=glRenderTexEx;
    mGLProgramSpherical=glPSpherical;
    switch (mMode){
        case RM_NORMAL:
            glGenBuffers(1,&mGLBuffVid);
            break;
        case RM_STEREO:
            glGenBuffers(1,&mGLBuffVidLeft);
            glGenBuffers(1,&mGLBuffVidRight);
            break;
        case RM_Degree360:
            glGenBuffers(1,&mGLBuffSphereVertices);
            //glGenBuffers(1,&mGLBuffSphereIndices);
            //GLProgramSpherical::uploadToGPU(mSphere,mGLBuffSphereVertices,mGLBuffSphereIndices);
            mSphere.uploadToGPU(mGLBuffSphereVertices);
            break;
        case RM_PunchHole:
            glGenBuffers(1,&mGLBuffVid);
            break;
    }
    glGenBuffers(1,&mIndexBuffer);
}

void VideoRenderer::setupPosition() {
    mPositionDebug.setWorldPositionDebug(mX,mY,mZ,mWidth,mHeight);
    //We need the indices unless 360 degree rendering
    if(mMode==RM_NORMAL ||mMode==RM_STEREO){
        const auto vid0=TexturedGeometry::makeTesselatedVideoCanvas(glm::vec3(mX, mY, mZ),
                                                    mWidth, mHeight, TESSELATION_FACTOR, 0.0f,
                                                    1.0f);
        GLBufferHelper::allocateGLBufferStatic(mGLBuffVid,vid0.vertices);
        GLBufferHelper::allocateGLBufferStatic(mIndexBuffer,vid0.indices);
        const auto vid1=TexturedGeometry::makeTesselatedVideoCanvas(glm::vec3(mX, mY, mZ),
                                                    mWidth, mHeight, TESSELATION_FACTOR, 0.0f,
                                                    0.5f);
        GLBufferHelper::allocateGLBufferStatic(mGLBuffVidLeft,vid1.vertices);
        const auto vid2=TexturedGeometry::makeTesselatedVideoCanvas( glm::vec3(mX, mY, mZ),
                                                    mWidth, mHeight, TESSELATION_FACTOR, 0.5f,
                                                    0.5f);
        GLBufferHelper::allocateGLBufferStatic(mGLBuffVidRight,vid2.vertices);
        nIndicesVideoCanvas=vid0.indices.size();
    }else if(mMode==RM_PunchHole){
        GLProgramVC::Vertex tmp[6];
        ColoredGeometry::makeColoredRect(tmp,glm::vec3(mX,mY,mZ),glm::vec3(mWidth,0,0),glm::vec3(0,mHeight,0),
                                         Color::TRANSPARENT);
        GLBufferHelper::allocateGLBufferStatic(mGLBuffVid,tmp,sizeof(tmp));

        ColoredGeometry::makeColoredRect(tmp,glm::vec3(mX,mY,mZ),glm::vec3(mWidth*5,0,0),glm::vec3(0,mHeight*10,0),
                                         Color::RED);
        GLBufferHelper::allocateGLBufferStatic(mGLBuffVidPunchHole,tmp,sizeof(tmp));
    }
}

void VideoRenderer::punchHole(glm::mat4x4 ViewM, glm::mat4x4 ProjM) {
    mGLRenderGeometry.beforeDraw(mGLBuffVid);
    mGLRenderGeometry.draw(glm::value_ptr(ViewM), glm::value_ptr(ProjM), 0, 2 * 3,GL_TRIANGLES);
    mGLRenderGeometry.afterDraw();
}

void VideoRenderer::punchHole2(glm::mat4x4 ViewM, glm::mat4x4 ProjM) {
    mGLRenderGeometry.beforeDraw(mGLBuffVidPunchHole);
    mGLRenderGeometry.draw(glm::value_ptr(ViewM), glm::value_ptr(ProjM), 0, 2 * 3,GL_TRIANGLES);
    mGLRenderGeometry.afterDraw();
}

void VideoRenderer::drawVideoCanvas(glm::mat4x4 ViewM, glm::mat4x4 ProjM, bool leftEye) {
    if(mMode==RM_Degree360){
        drawVideoCanvas360(ViewM,ProjM);
    }else if(mMode==RM_NORMAL || mMode==RM_STEREO){
        GLuint buff=mMode==RM_NORMAL ? mGLBuffVid : leftEye ? mGLBuffVidLeft : mGLBuffVidRight;
        mGLRenderTexEx->beforeDraw(buff,mVideoTexture);
        mGLRenderTexEx->drawIndexed(ViewM,ProjM,0,nIndicesVideoCanvas,mIndexBuffer);
        mGLRenderTexEx->afterDraw();
    }
    //punch hole handled differently
    /*GLuint buff;
    switch(mMode){
        case RM_NORMAL: buff=mGLBuffVid; //no 3D video
            break;
        case RM_STEREO:
            buff=leftEye ? mGLBuffVidLeft : mGLBuffVidRight; //3D video - left and right eye
            break;
        default:
            buff=0;
            break;
    }
    mGLRenderTexEx->beforeDraw(buff);
    mGLRenderTexEx->drawIndexed(ViewM,ProjM,0,nIndicesVideoCanvas,mIndexBuffer);
    mGLRenderTexEx->afterDraw();*/
    //We render the debug rectangle after the other one such that it always appears when enabled (overdraw)
    mPositionDebug.drawGLDebug(ViewM,ProjM);
    GLHelper::checkGlError("VideoRenderer::drawVideoCanvas");
}

void VideoRenderer::drawVideoCanvas360(glm::mat4x4 ViewM, glm::mat4x4 ProjM) {
    if(mMode!=VIDEO_RENDERING_MODE::RM_Degree360){
        throw "mMode!=VIDEO_RENDERING_MODE::Degree360";
    }
    //Default view direction, the gvr sphere is slightly different -
    //For 'normal'video its layout is fine, but for insta360 video the default view direction is wrong
    glm::mat4x4 modelMatrix=glm::rotate(glm::mat4(1.0F),glm::radians(90.0F), glm::vec3(0,0,-1));

    mGLProgramSpherical->beforeDraw(mGLBuffSphereVertices,mVideoTexture);
    mGLProgramSpherical->draw(ViewM*modelMatrix,ProjM,mSphere.getVertexCount());
    mGLProgramSpherical->afterDraw();
    GLHelper::checkGlError("VideoRenderer::drawVideoCanvas360");
}

void VideoRenderer::initUpdateTexImageJAVA(JNIEnv *env, jobject obj,jobject surfaceTexture) {
#ifdef FPV_VR_USE_JAVA_FOR_SURFACE_TEXTURE_UPDATE
    jclass jcSurfaceTexture = env->FindClass("android/graphics/SurfaceTexture");
    LOGD("SurfaceTextureStart");
    if ( jcSurfaceTexture == nullptr ) {
        LOGD( "FindClass( SurfaceTexture ) failed");
    }
    // find the constructor that takes an int
    jmethodID constructor = env->GetMethodID( jcSurfaceTexture, "<init>", "(I)V" );
    if ( constructor == nullptr) {
        LOGD( "GetMethodID( <init> ) failed" );
    }
    localRefSurfaceTexture = env->NewGlobalRef( surfaceTexture);
    if ( localRefSurfaceTexture == nullptr ) {
        LOGD( "NewGlobalRef() failed" );
    }
    // Now that we have a globalRef, we can free the localRef
    env->DeleteLocalRef( surfaceTexture );
    //get the java methods that can be called with a valid surfaceTexture instance and JNI env
    updateTexImageMethodId = env->GetMethodID( jcSurfaceTexture, "updateTexImage", "()V" );
    if ( !updateTexImageMethodId ) {
        LOGD("couldn't get updateTexImageMethodId" );
    }
    getTimestampMethodId = env->GetMethodID( jcSurfaceTexture, "getTimestamp", "()J" );
    if ( !getTimestampMethodId ) {
        LOGD( "couldn't get getTimestampMethodId" );
    }
#else
    mSurfaceTexture=ASurfaceTexture_fromSurfaceTexture(env,surfaceTexture);
#endif
}

void VideoRenderer::deleteUpdateTexImageJAVA(JNIEnv *env, jobject obj) {
#ifdef FPV_VR_USE_JAVA_FOR_SURFACE_TEXTURE_UPDATE
    env->DeleteGlobalRef(localRefSurfaceTexture);
#endif
}

void VideoRenderer::updateTexImageJAVA(JNIEnv* env) {
#ifdef FPV_VR_USE_JAVA_FOR_SURFACE_TEXTURE_UPDATE
    env->CallVoidMethod( localRefSurfaceTexture, updateTexImageMethodId );
#else
    ASurfaceTexture_updateTexImage(mSurfaceTexture);
#endif
}



