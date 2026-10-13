#include <Camera.h>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "VK/vk_engine.h"

const int g_cameraSpeed = 5;

void Camera::update()
{
    auto& engine = VulkanEngine::get();

    glm::mat4 cameraRotation = getRotationMatrix();
    position += glm::vec3(cameraRotation * glm::vec4(velocity * VulkanEngine::getTimeDelta(), 0.f));
    aspect = (float)engine.m_windowExtent.width / (float)engine.m_windowExtent.height;

    m_view = getViewMatrix();
    m_proj = getProjectionMatrix();
    m_viewProj = m_proj * m_view;

    // near
    m_frustumPlanes[0].vec.x = m_viewProj[0].w + m_viewProj[0].z;
    m_frustumPlanes[0].vec.y = m_viewProj[1].w + m_viewProj[1].z;
    m_frustumPlanes[0].vec.z = m_viewProj[2].w + m_viewProj[2].z;
    m_frustumPlanes[0].vec.w = m_viewProj[3].w + m_viewProj[3].z;
    m_frustumPlanes[0].vec /= glm::length(m_frustumPlanes[0].struc.norm);

    // far
    m_frustumPlanes[1].vec.x = m_viewProj[0].w - m_viewProj[0].z;
    m_frustumPlanes[1].vec.y = m_viewProj[1].w - m_viewProj[1].z;
    m_frustumPlanes[1].vec.z = m_viewProj[2].w - m_viewProj[2].z;
    m_frustumPlanes[1].vec.w = m_viewProj[3].w - m_viewProj[3].z;
    m_frustumPlanes[1].vec /= glm::length(m_frustumPlanes[1].struc.norm);

    // bottom
    m_frustumPlanes[2].vec.x = m_viewProj[0].w + m_viewProj[0].y;
    m_frustumPlanes[2].vec.y = m_viewProj[1].w + m_viewProj[1].y;
    m_frustumPlanes[2].vec.z = m_viewProj[2].w + m_viewProj[2].y;
    m_frustumPlanes[2].vec.w = m_viewProj[3].w + m_viewProj[3].y;
    m_frustumPlanes[2].vec /= glm::length(m_frustumPlanes[2].struc.norm);

    // top
    m_frustumPlanes[3].vec.x = m_viewProj[0].w - m_viewProj[0].y;
    m_frustumPlanes[3].vec.y = m_viewProj[1].w - m_viewProj[1].y;
    m_frustumPlanes[3].vec.z = m_viewProj[2].w - m_viewProj[2].y;
    m_frustumPlanes[3].vec.w = m_viewProj[3].w - m_viewProj[3].y;
    m_frustumPlanes[3].vec /= glm::length(m_frustumPlanes[3].struc.norm);

    // left
    m_frustumPlanes[4].vec.x = m_viewProj[0].w + m_viewProj[0].x;
    m_frustumPlanes[4].vec.y = m_viewProj[1].w + m_viewProj[1].x;
    m_frustumPlanes[4].vec.z = m_viewProj[2].w + m_viewProj[2].x;
    m_frustumPlanes[4].vec.w = m_viewProj[3].w + m_viewProj[3].x;
    m_frustumPlanes[4].vec /= glm::length(m_frustumPlanes[4].struc.norm);

    // right
    m_frustumPlanes[5].vec.x = m_viewProj[0].w - m_viewProj[0].x;
    m_frustumPlanes[5].vec.y = m_viewProj[1].w - m_viewProj[1].x;
    m_frustumPlanes[5].vec.z = m_viewProj[2].w - m_viewProj[2].x;
    m_frustumPlanes[5].vec.w = m_viewProj[3].w - m_viewProj[3].x;
    m_frustumPlanes[5].vec /= glm::length(m_frustumPlanes[5].struc.norm);
}

void Camera::processSDLEvent(SDL_Event& e)
{
    if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_w) { velocity.z = -g_cameraSpeed; }
        if (e.key.keysym.sym == SDLK_s) { velocity.z = g_cameraSpeed; }
        if (e.key.keysym.sym == SDLK_a) { velocity.x = -g_cameraSpeed; }
        if (e.key.keysym.sym == SDLK_d) { velocity.x = g_cameraSpeed; }
    }

    if (e.type == SDL_KEYUP) {
        if (e.key.keysym.sym == SDLK_w) { velocity.z = 0; }
        if (e.key.keysym.sym == SDLK_s) { velocity.z = 0; }
        if (e.key.keysym.sym == SDLK_a) { velocity.x = 0; }
        if (e.key.keysym.sym == SDLK_d) { velocity.x = 0; }
    }

    if (e.type == SDL_MOUSEMOTION) {
        yaw += (float)e.motion.xrel / 200.f;
        pitch -= (float)e.motion.yrel / 200.f;
    }
}

glm::mat4 Camera::getViewMatrix()
{
    // to create a correct model view, we need to move the world in opposite
    // direction to the camera
    //  so we will create the camera model matrix and invert
    glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), position);
    glm::mat4 cameraRotation = getRotationMatrix();
    return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Camera::getProjectionMatrix()
{
    // camera projection
    glm::mat4 projection = glm::perspective(glm::radians(fov), aspect, zFar, zNear);

    // invert the Y direction on projection matrix so that we are more similar
    // to opengl and gltf axis
    projection[1][1] *= -1;

    return projection;
}

glm::mat4 Camera::getRotationMatrix()
{
    // fairly typical FPS style camera. we join the pitch and yaw rotations into
    // the final rotation matrix

    glm::quat pitchRotation = glm::angleAxis(pitch, glm::vec3{ 1.f, 0.f, 0.f });
    glm::quat yawRotation = glm::angleAxis(yaw, glm::vec3{ 0.f, -1.f, 0.f });

    return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}