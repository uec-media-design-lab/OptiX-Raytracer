#include "camera.h"
#include <prayground/app/app_runner.h>
#include <prayground/math/util.h>

namespace prayground {

void Camera::enableTracking(std::shared_ptr<Window> window)
{
    window->events().mouseDragged.bindFunction([&](float x, float y, int button){ return this->mouseDragged(x, y, button); });
    window->events().mouseScrolled.bindFunction([&](float xoffset, float yoffset){ return this->mouseScrolled(xoffset, yoffset); });
}

void Camera::disableTracking()
{
    TODO_MESSAGE();
}

void Camera::mouseDragged(float x, float y, int button)
{
    float deltaX = x - pgGetPreviousMousePosition().x;
    float deltaY = y - pgGetPreviousMousePosition().y;
    float cam_length = length(this->origin());
    float3 cam_dir = normalize(this->origin() - this->lookat());

    float theta = acosf(cam_dir.y);
    float phi = atan2(cam_dir.z, cam_dir.x);

    theta = std::min(constants::pi - 0.01f, std::max(0.01f, theta - radians(deltaY * 0.25f)));
    phi += radians(deltaX * 0.25f);

    float cam_x = cam_length * sinf(theta) * cosf(phi);
    float cam_y = cam_length * cosf(theta);
    float cam_z = cam_length * sinf(theta) * sinf(phi);

    this->setOrigin({ cam_x, cam_y, cam_z });
}

void Camera::mouseScrolled(float xoffset, float yoffset)
{
    float zoom = yoffset < 0 ? 1.1f : 1.0f / 1.1f;
    this->setOrigin(this->lookat() + (this->origin() - this->lookat()) * zoom);
}

} // ::prayground