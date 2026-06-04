#pragma once
#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <limits>
#include <tri.h>
#include <vector>

struct BVH;
struct CompactBVH;
extern std::vector<BVH> globalBVH;
extern std::vector<CompactBVH> globalCompactBVH;

inline constexpr uint32_t BVH_LEAF_TRIANGLE_COUNT = 6;
inline constexpr int BVH_SAH_BINS = 8;

inline float bvhSurfaceArea(const glm::vec3& min, const glm::vec3& max) {
	glm::vec3 extent = glm::max(max - min, glm::vec3(0.0f));
	return 2.0f * (extent.x * extent.y + extent.x * extent.z + extent.y * extent.z);
}

struct BVH {
	glm::vec3 min = glm::vec3(0.0f);
	glm::vec3 max = glm::vec3(0.0f);
	glm::vec3 splitPoint = glm::vec3(0.0f);

	uint32_t children[2] = { UINT32_MAX, UINT32_MAX };

	uint32_t startIndex = 0;
	uint32_t endIndex = 0;
	uint32_t next = 0;

	BVH(uint32_t startIndex, uint32_t endIndex, std::vector<Tri>& tris, std::vector<BVH>& globalBVH) :
		startIndex(startIndex), endIndex(endIndex) {

		calculateAABB(tris);
		avgSplit(tris);

		uint32_t count = 0;
		if (endIndex >= startIndex && startIndex < tris.size()) {
			uint32_t clampedEnd = std::min<uint32_t>(endIndex, uint32_t(tris.size() - 1));
			count = clampedEnd - startIndex + 1;
		}

		if (count > BVH_LEAF_TRIANGLE_COUNT) {
			createChildren(tris, globalBVH);
			calculateNextNeighbor();
		}
	}

	BVH()
		: min(0.0f),
		max(0.0f),
		splitPoint(0.0f),
		startIndex(0),
		endIndex(0),
		next(0) {

		children[0] = UINT32_MAX;
		children[1] = UINT32_MAX;
	}

	void calculateAABB(std::vector<Tri>& tris) {
		min = glm::vec3(std::numeric_limits<float>::max());
		max = glm::vec3(std::numeric_limits<float>::lowest());

		if (startIndex > endIndex || startIndex >= tris.size()) return;

		uint32_t clampedEnd = std::min<uint32_t>(endIndex, uint32_t(tris.size() - 1));
		for (uint32_t i = startIndex; i <= clampedEnd; ++i) {
			min = glm::min(min, tris[i].min);
			max = glm::max(max, tris[i].max);
		}
	}

	void avgSplit(std::vector<Tri>& tris) {
		splitPoint = glm::vec3(0.0f);

		if (startIndex > endIndex || startIndex >= tris.size()) return;

		uint32_t clampedEnd = std::min<uint32_t>(endIndex, uint32_t(tris.size() - 1));
		uint32_t count = clampedEnd - startIndex + 1;
		if (count == 0) return;

		for (uint32_t i = startIndex; i <= clampedEnd; ++i) {
			splitPoint += tris[i].center;
		}

		splitPoint /= float(count);
	}

	void createChildren(std::vector<Tri>& tris, std::vector<BVH>& globalBVH) {

		glm::vec3 extent = max - min;
		uint32_t clampedEnd = std::min<uint32_t>(endIndex, uint32_t(tris.size() - 1));

		int bestAxis = -1;
		int bestSplit = -1;
		float bestCost = std::numeric_limits<float>::max();

		for (int axis = 0; axis < 3; ++axis) {
			float centroidMin = std::numeric_limits<float>::max();
			float centroidMax = std::numeric_limits<float>::lowest();

			for (uint32_t i = startIndex; i <= clampedEnd; ++i) {
				centroidMin = std::min(centroidMin, tris[i].center[axis]);
				centroidMax = std::max(centroidMax, tris[i].center[axis]);
			}

			float centroidExtent = centroidMax - centroidMin;
			if (centroidExtent <= 0.0f) {
				continue;
			}

			std::array<uint32_t, BVH_SAH_BINS> counts{};
			std::array<glm::vec3, BVH_SAH_BINS> binMin;
			std::array<glm::vec3, BVH_SAH_BINS> binMax;

			for (int i = 0; i < BVH_SAH_BINS; ++i) {
				binMin[i] = glm::vec3(std::numeric_limits<float>::max());
				binMax[i] = glm::vec3(std::numeric_limits<float>::lowest());
			}

			for (uint32_t i = startIndex; i <= clampedEnd; ++i) {
				int bin = int(((tris[i].center[axis] - centroidMin) / centroidExtent) * float(BVH_SAH_BINS));
				bin = std::clamp(bin, 0, BVH_SAH_BINS - 1);
				counts[bin]++;
				binMin[bin] = glm::min(binMin[bin], tris[i].min);
				binMax[bin] = glm::max(binMax[bin], tris[i].max);
			}

			std::array<uint32_t, BVH_SAH_BINS> leftCounts{};
			std::array<uint32_t, BVH_SAH_BINS> rightCounts{};
			std::array<glm::vec3, BVH_SAH_BINS> leftMin;
			std::array<glm::vec3, BVH_SAH_BINS> leftMax;
			std::array<glm::vec3, BVH_SAH_BINS> rightMin;
			std::array<glm::vec3, BVH_SAH_BINS> rightMax;

			glm::vec3 runningMin(std::numeric_limits<float>::max());
			glm::vec3 runningMax(std::numeric_limits<float>::lowest());
			uint32_t runningCount = 0;
			for (int i = 0; i < BVH_SAH_BINS; ++i) {
				if (counts[i] > 0) {
					runningMin = glm::min(runningMin, binMin[i]);
					runningMax = glm::max(runningMax, binMax[i]);
				}
				runningCount += counts[i];
				leftCounts[i] = runningCount;
				leftMin[i] = runningMin;
				leftMax[i] = runningMax;
			}

			runningMin = glm::vec3(std::numeric_limits<float>::max());
			runningMax = glm::vec3(std::numeric_limits<float>::lowest());
			runningCount = 0;
			for (int i = BVH_SAH_BINS - 1; i >= 0; --i) {
				if (counts[i] > 0) {
					runningMin = glm::min(runningMin, binMin[i]);
					runningMax = glm::max(runningMax, binMax[i]);
				}
				runningCount += counts[i];
				rightCounts[i] = runningCount;
				rightMin[i] = runningMin;
				rightMax[i] = runningMax;
			}

			for (int split = 0; split < BVH_SAH_BINS - 1; ++split) {
				uint32_t leftCount = leftCounts[split];
				uint32_t rightCount = rightCounts[split + 1];
				if (leftCount == 0 || rightCount == 0) {
					continue;
				}

				float cost = bvhSurfaceArea(leftMin[split], leftMax[split]) * float(leftCount) +
					bvhSurfaceArea(rightMin[split + 1], rightMax[split + 1]) * float(rightCount);
				if (cost < bestCost) {
					bestCost = cost;
					bestAxis = axis;
					bestSplit = split;
				}
			}
		}

		uint32_t aIdx = startIndex;

		if (bestAxis >= 0) {
			float centroidMin = std::numeric_limits<float>::max();
			float centroidMax = std::numeric_limits<float>::lowest();
			for (uint32_t i = startIndex; i <= clampedEnd; ++i) {
				centroidMin = std::min(centroidMin, tris[i].center[bestAxis]);
				centroidMax = std::max(centroidMax, tris[i].center[bestAxis]);
			}
			float centroidExtent = centroidMax - centroidMin;

			for (uint32_t i = startIndex; i <= clampedEnd; ++i) {
				int bin = int(((tris[i].center[bestAxis] - centroidMin) / centroidExtent) * float(BVH_SAH_BINS));
				bin = std::clamp(bin, 0, BVH_SAH_BINS - 1);
				if (bin <= bestSplit) {
					if (i != aIdx) {
						std::swap(tris[i], tris[aIdx]);
					}
					aIdx++;
				}
			}
		}
		else {
			int axis = 0;
			if (extent.y > extent.x) axis = 1;
			if (extent.z > extent[axis]) axis = 2;

			uint32_t mid = startIndex + (clampedEnd - startIndex + 1) / 2;
			std::nth_element(
				tris.begin() + startIndex,
				tris.begin() + mid,
				tris.begin() + clampedEnd + 1,
				[axis](const Tri& a, const Tri& b) {
					return a.center[axis] < b.center[axis];
				}
			);
			aIdx = mid;
		}

		uint32_t leftCount = (aIdx > startIndex) ? (aIdx - startIndex) : 0;
		uint32_t rightCount = (clampedEnd >= aIdx && aIdx < tris.size()) ? (clampedEnd - aIdx + 1) : 0;

		if (leftCount == 0 || rightCount == 0) {
			return;
		}

		uint32_t childAIdx = uint32_t(globalBVH.size());
		globalBVH.emplace_back();
		globalBVH[childAIdx] = BVH(startIndex, aIdx - 1, tris, globalBVH);

		uint32_t childBIdx = uint32_t(globalBVH.size());
		globalBVH.emplace_back();
		globalBVH[childBIdx] = BVH(aIdx, endIndex, tris, globalBVH);

		children[0] = childAIdx;
		children[1] = childBIdx;
	}

	inline void calculateNextNeighbor() {

		next = 0;

		for (int i = 0; i < 2; ++i) {
			uint32_t idx = children[i];

			if (idx == UINT32_MAX) continue;

			BVH& child = globalBVH[idx];

			next += child.next;

			next++;
		}
	}
};

struct CompactBVH {
	glm::vec3 min;
	glm::vec3 max;

	union {
		uint32_t startIndex;
		uint32_t missLink;
	};

	uint32_t triCount;
};
