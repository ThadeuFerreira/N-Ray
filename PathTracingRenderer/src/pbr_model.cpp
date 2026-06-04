#include "pbr_model.h"
#include "globalParams.h"

void PTModel::updateTris(Data& data) {
	for (uint32_t triIdx : tris) {
		if (triIdx < data.tris.size()) {
			data.tris[triIdx].doubleSided = doubleSided;
		}
	}
}
