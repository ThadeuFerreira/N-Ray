#pragma once
#include <cstdint>
#include <glm/glm.hpp>

// Cache-friendly intersection record, kept in a parallel array to `data.tris`.
// BVH traversal touches only this (44 bytes) instead of the ~160-byte fat `Tri`;
// the full triangle (normals, material) is read once, on the final hit, via `idx`.
struct TriIntersect {
	glm::vec3 a;
	glm::vec3 eA;
	glm::vec3 eB;
	uint32_t idx;
	uint32_t doubleSided;
};

struct Tri {
	glm::vec3 a;
	glm::vec3 b;
	glm::vec3 c;
	glm::vec3 aN;
	glm::vec3 bN;
	glm::vec3 cN;
	glm::vec3 eA;
	glm::vec3 eB;
	glm::vec3 normal;
	glm::vec3 min;
	glm::vec3 max;
	glm::vec3 center;
	uint32_t idx;
	uint32_t materialIdx;
	bool doubleSided;
	uint32_t modelIdx;

	Tri(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 aN, glm::vec3 bN, glm::vec3 cN, uint32_t materialIdx, uint32_t modelIdx, bool doubleSided)
		: a(a), b(b), c(c), aN(aN), bN(bN), cN(cN), idx(0), materialIdx(materialIdx), doubleSided(doubleSided), modelIdx(modelIdx) {
		calculateNormal();
		calculateAABB();
		calculateCenter();
	}

	void calculateNormal() {
		eA = b - a;
		eB = c - a;

		glm::vec3 faceNormal = glm::cross(eA, eB);
		float lenSq = glm::dot(faceNormal, faceNormal);
		normal = lenSq > 0.00000001f ? glm::normalize(faceNormal) : glm::vec3(0.0f, 0.0f, 1.0f);
	}

	void calculateAABB() {
		min = glm::min(a, glm::min(b, c));
		max = glm::max(a, glm::max(b, c));
	}

	void calculateCenter() {
		center = (a + b + c) / 3.0f;
	}
};
