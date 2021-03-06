#pragma once
#include <array>
#include <math.h>
#include <glm/glm.hpp>

namespace vkTools
{
	class Frustum
	{
	public:
		enum side { LEFT = 0, RIGHT = 1, TOP = 2, BOTTOM = 3, BACK = 4, FRONT = 5 };
		std::array<glm::vec4, 6> planes;

		void update(Matrix &matrix)
		{
			planes[LEFT].x = matrix.m[3] + matrix.m[0];
			planes[LEFT].y = matrix.m[7] + matrix.m[4];
			planes[LEFT].z = matrix.m[11] + matrix.m[8];
			planes[LEFT].w = matrix.m[15] + matrix.m[12];

			planes[RIGHT].x = matrix.m[3] - matrix.m[0];
			planes[RIGHT].y = matrix.m[7] - matrix.m[4];
			planes[RIGHT].z = matrix.m[11] - matrix.m[8];
			planes[RIGHT].w = matrix.m[15] - matrix.m[12];

			planes[TOP].x = matrix.m[3] - matrix.m[1];
			planes[TOP].y = matrix.m[7] - matrix.m[5];
			planes[TOP].z = matrix.m[11] - matrix.m[9];
			planes[TOP].w = matrix.m[15] - matrix.m[13];

			planes[BOTTOM].x = matrix.m[3] + matrix.m[1];
			planes[BOTTOM].y = matrix.m[7] + matrix.m[5];
			planes[BOTTOM].z = matrix.m[11] + matrix.m[9];
			planes[BOTTOM].w = matrix.m[15] + matrix.m[13];

			planes[BACK].x = matrix.m[3] + matrix.m[2];
			planes[BACK].y = matrix.m[7] + matrix.m[6];
			planes[BACK].z = matrix.m[11] + matrix.m[10];
			planes[BACK].w = matrix.m[15] + matrix.m[14];

			planes[FRONT].x = matrix.m[3] - matrix.m[2];
			planes[FRONT].y = matrix.m[7] - matrix.m[6];
			planes[FRONT].z = matrix.m[11] - matrix.m[10];
			planes[FRONT].w = matrix.m[15] - matrix.m[14];

			//planes[LEFT].x = matrix[0].w + matrix[0].x;
			//planes[LEFT].y = matrix[1].w + matrix[1].x;
			//planes[LEFT].z = matrix[2].w + matrix[2].x;
			//planes[LEFT].w = matrix[3].w + matrix[3].x;

			//planes[RIGHT].x = matrix[0].w - matrix[0].x;
			//planes[RIGHT].y = matrix[1].w - matrix[1].x;
			//planes[RIGHT].z = matrix[2].w - matrix[2].x;
			//planes[RIGHT].w = matrix[3].w - matrix[3].x;

			//planes[TOP].x = matrix[0].w - matrix[0].y;
			//planes[TOP].y = matrix[1].w - matrix[1].y;
			//planes[TOP].z = matrix[2].w - matrix[2].y;
			//planes[TOP].w = matrix[3].w - matrix[3].y;

			//planes[BOTTOM].x = matrix[0].w + matrix[0].y;
			//planes[BOTTOM].y = matrix[1].w + matrix[1].y;
			//planes[BOTTOM].z = matrix[2].w + matrix[2].y;
			//planes[BOTTOM].w = matrix[3].w + matrix[3].y;

			//planes[BACK].x = matrix[0].w + matrix[0].z;
			//planes[BACK].y = matrix[1].w + matrix[1].z;
			//planes[BACK].z = matrix[2].w + matrix[2].z;
			//planes[BACK].w = matrix[3].w + matrix[3].z;

			//planes[FRONT].x = matrix[0].w - matrix[0].z;
			//planes[FRONT].y = matrix[1].w - matrix[1].z;
			//planes[FRONT].z = matrix[2].w - matrix[2].z;
			//planes[FRONT].w = matrix[3].w - matrix[3].z;

			for (auto i = 0; i < planes.size(); i++)
			{
				float length = sqrtf(planes[i].x * planes[i].x + planes[i].y * planes[i].y + planes[i].z * planes[i].z);
				planes[i] /= length;
			}
		}
		
		bool checkSphere(Vector3 &pos, float radius)
		{
			for (auto i = 0; i < planes.size(); i++)
			{
				if ((planes[i].x * pos.x) + (planes[i].y * pos.y) + (planes[i].z * pos.z) + planes[i].w <= -radius)
				{
					return false;
				}
			}
			return true;
		}
	};
}
