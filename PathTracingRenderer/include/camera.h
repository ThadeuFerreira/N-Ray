#pragma once
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/rotate_vector.hpp>

struct PTCam {

	glm::vec3 camPos = { 0.0f, -16.0f, 1.7f };
	glm::vec3 camTarget = { 0.0f, 0.0f, 1.7f };

	float targetDist = 1.0f;

	float camSpeed = 10.0f;
	float fov = 45.0f;

	float focalLengthMM = 35.0f;
	float focalLength = focalLengthMM / 1000.0f;
	float aperture = 0.0f;
	float fStop = 2.0f;
	float ISO = 1.0f;
	float focusDist = 16.0f;

	float sensorSize = 35.0f;

	bool clickDof = false;

	glm::vec3 camNormal = { 0.0f, 1.0f, 0.0f };
	glm::vec3 worldUp = { 0.0f, 0.0f, 1.0f };
	glm::vec3 orbitCenter = { 0.0f, 0.0f, 0.0f };
	glm::vec3 right = glm::normalize(glm::cross(camNormal, worldUp));
	glm::vec3 up = glm::normalize(glm::cross(right, camNormal));
	float halfFovRadians = glm::radians(fov) * 0.5f;
	float verticalScale = tan(halfFovRadians);

	glm::vec3 targetNormal = { 0.0f, 1.0f, 0.0f };

	glm::vec3 focalPoint;

	float fovH;
	float fovV;

	bool keyDownSample(int key, Params& params) {
		if (IsKeyDown(key)) {
			params.shouldSample = false;
			params.enableSampling = false;
			params.renderInvalidated = true;
			return true;
		}
		else {
			params.enableSampling = true;
			return false;
		}
	}

	bool mouseDownSample(int key, Params& params) {
		if (IsMouseButtonDown(key)) {
			params.shouldSample = false;
			params.enableSampling = false;
			params.renderInvalidated = true;
			return true;
		}
		else {
			params.enableSampling = true;
			return false;
		}
	}

	float mouseSensitivity = 0.01f;
	float zoomSensitivity = 0.12f;

	glm::vec3 safeNormalize(glm::vec3 value, glm::vec3 fallback) {
		float lenSq = glm::dot(value, value);
		if (!std::isfinite(lenSq) || lenSq <= 0.00000001f) {
			return fallback;
		}
		return glm::normalize(value);
	}

	void cameraLogic(Params& params, float& ratio) {
		float currentSpeed = camSpeed * params.dt;
		bool rightMouseDown = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
		bool middleMouseDown = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);

		if (!params.isMouseHoveringUI && rightMouseDown && !middleMouseDown) {
			if (keyDownSample(KEY_A, params)) camPos -= right * currentSpeed;
			if (keyDownSample(KEY_D, params)) camPos += right * currentSpeed;
			if (keyDownSample(KEY_S, params)) camPos -= targetNormal * currentSpeed;
			if (keyDownSample(KEY_W, params)) camPos += targetNormal * currentSpeed;
			if (keyDownSample(KEY_LEFT_CONTROL, params)) camPos.z -= currentSpeed;
			if (keyDownSample(KEY_LEFT_SHIFT, params)) camPos.z += currentSpeed;
		}

		if (!params.isMouseHoveringUI) {
			float wheel = GetMouseWheelMove();
			if (wheel != 0.0f) {
				glm::vec3 zoomPivot = (middleMouseDown ? glm::vec3(0.0f, 0.0f, 0.0f) : orbitCenter);
				glm::vec3 offset = camPos - zoomPivot;
				float distance = glm::length(offset);
				if (!std::isfinite(distance) || distance <= 0.0001f) {
					distance = 1.0f;
					offset = glm::vec3(0.0f, -distance, 0.0f);
				}

				float zoomFactor = std::pow(1.0f - zoomSensitivity, wheel);
				float nextDistance = std::clamp(distance * zoomFactor, 0.01f, 10000.0f);
				camPos = zoomPivot + safeNormalize(offset, glm::vec3(0.0f, -1.0f, 0.0f)) * nextDistance;
				targetNormal = safeNormalize(zoomPivot - camPos, targetNormal);

				params.shouldSample = false;
				params.enableSampling = false;
				params.renderInvalidated = true;
			}
		}

		glm::vec2 mDelta = { GetMouseDelta().x, GetMouseDelta().y };
		if (!params.isMouseHoveringUI && (mDelta.x != 0.0f || mDelta.y != 0.0f)) {
			if (middleMouseDown || (rightMouseDown && !middleMouseDown)) {
				params.shouldSample = false;
				params.enableSampling = false;
				params.renderInvalidated = true;

				const glm::vec3 orbitPivot = middleMouseDown ? glm::vec3(0.0f, 0.0f, 0.0f) : orbitCenter;
				bool teleported = false;
				if (GetMousePosition().x < 0.0f) {
					SetMousePosition(params.screenSize.x, int(GetMousePosition().y));
					teleported = true;
				}
				if (GetMousePosition().x > params.screenSize.x) {
					SetMousePosition(0, int(GetMousePosition().y));
					teleported = true;
				}

				if (GetMousePosition().y < 0.0f) {
					SetMousePosition(int(GetMousePosition().x), params.screenSize.y);
					teleported = true;
				}
				if (GetMousePosition().y > params.screenSize.y) {
					SetMousePosition(int(GetMousePosition().x), 0);
					teleported = true;
				}

				if (!teleported) {
					float yaw = -mDelta.x * mouseSensitivity;
					float pitch = -mDelta.y * ratio * mouseSensitivity;

					if (middleMouseDown) {
						glm::vec3 offset = camPos - orbitPivot;
						float distance = glm::length(offset);
						if (!std::isfinite(distance) || distance <= 0.0001f) {
							distance = std::max(focusDist, 1.0f);
							offset = glm::vec3(0.0f, -distance, 0.0f);
						}

						glm::vec3 yawedOffset = glm::rotate(offset, yaw, worldUp);
						glm::vec3 forwardAfterYaw = safeNormalize(-yawedOffset, targetNormal);
						glm::vec3 pitchAxis = safeNormalize(glm::cross(forwardAfterYaw, worldUp), right);
						glm::vec3 pitchedOffset = glm::rotate(yawedOffset, pitch, pitchAxis);

						float upDot = glm::dot(safeNormalize(pitchedOffset, yawedOffset), worldUp);
						if (std::abs(upDot) < 0.98f) {
							camPos = orbitPivot + pitchedOffset;
						}
						else {
							camPos = orbitPivot + yawedOffset;
						}
						targetNormal = safeNormalize(orbitPivot - camPos, targetNormal);
					}
					else {
						glm::vec3 forward = glm::rotate(targetNormal, yaw, worldUp);
						glm::vec3 pitchAxis = safeNormalize(glm::cross(forward, worldUp), right);
						glm::vec3 rotated = glm::rotate(forward, pitch, pitchAxis);

						float upDot = glm::dot(safeNormalize(rotated, targetNormal), worldUp);
						if (std::abs(upDot) < 0.98f) {
							targetNormal = safeNormalize(rotated, targetNormal);
						}
						else {
							targetNormal = safeNormalize(forward, targetNormal);
						}
					}
				}
			}
		}

		targetNormal = safeNormalize(targetNormal, camNormal);
		camTarget = camPos + targetNormal * 10.0f;

		halfFovRadians = glm::radians(fov) * 0.5f;
		verticalScale = tan(halfFovRadians);

		glm::vec3 camD = camTarget - camPos;

		camNormal = safeNormalize(camD, targetNormal);
		targetNormal = camNormal;
		targetDist = glm::length(camD);

		right = safeNormalize(glm::cross(camNormal, worldUp), right);
		up = safeNormalize(glm::cross(right, camNormal), up);

		focalLength = focalLengthMM / 1000.0f;
		float sensorWidth = sensorSize / 1000.0f;
		float sensorHeight = sensorWidth / ratio;

		fovH = 2.0f * glm::degrees(std::atan(sensorWidth / (2.0f * focalLength)));
		fovV = 2.0f * glm::degrees(std::atan(sensorHeight / (2.0f * focalLength)));

		focalPoint = camPos + camNormal * focalLength;
	}
};
