#include <app.h>

#include <cmath>
#include <rlgl.h>

void updateCamera3D() {
	cam3D.position = { myCam.camPos.x, myCam.camPos.y, myCam.camPos.z };
	cam3D.target = { myCam.camTarget.x, myCam.camTarget.y, myCam.camTarget.z };
	cam3D.up = { myCam.up.x, myCam.up.y, myCam.up.z };
	cam3D.fovy = myCam.fovV;
}

void drawRasterPreview() {
	rlBegin(RL_TRIANGLES);

	glm::vec3 camPos = myCam.camPos;

	for (size_t i = 0; i < data.tris.size(); i++) {
		Tri& tri = data.tris[i];
		const PBRMaterial& material = data.materials[tri.materialIdx];
		glm::vec3 lightDir = glm::normalize(tri.center - camPos);

		for (int j = 0; j < 3; j++) {
			glm::vec3 normal;
			glm::vec3 pos;

			if (j == 0) {
				normal = tri.aN;
				pos = tri.a;
			}
			else if (j == 1) {
				normal = tri.bN;
				pos = tri.b;
			}
			else {
				normal = tri.cN;
				pos = tri.c;
			}

			float light = std::fabs(glm::dot(lightDir, normal));
			float intensity = (light < 0.9f) ? (light * 0.9f) : (light * light);
			glm::vec3 col = glm::clamp(material.albedo * intensity, 0.0f, 1.0f);

			RenderPixel pixel = vec3ToRenderPixel(col);

			rlColor4ub(pixel.r, pixel.g, pixel.b, pixel.a);
			rlVertex3f(pos.x, pos.y, pos.z);
		}
	}

	rlEnd();
}
