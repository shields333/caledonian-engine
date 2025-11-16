#pragma once

#include <vk_types.h>
#include <SDL_events.h>

class Camera {
public:
    union FrustumPlane{
        glm::vec4 vec;
        struct Plane {
            glm::vec3 norm;
            float dist;
        }struc;
    };

    glm::vec3 velocity;
    glm::vec3 position;
    // vertical rotation
    float pitch{ 0.f };
    // horizontal rotation
    float yaw{ 0.f };

    glm::mat4 m_view;
    glm::mat4 m_proj;
    glm::mat4 m_viewProj;

    float fov = 70.f, aspect;
    const float zFar = 10000.f, zNear = 0.1f;

    std::array<FrustumPlane, 6> m_frustumPlanes;

    glm::mat4 getViewMatrix();
    glm::mat4 getProjectionMatrix();
    glm::mat4 getRotationMatrix();

    void processSDLEvent(SDL_Event& e);

    void update();
};