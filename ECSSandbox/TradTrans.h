#pragma once
#include "TradComp.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Trad
{
	class TradTrans
	{
	public:
		TradTrans();
		~TradTrans();
		void Init();
		glm::vec3 position{};
		glm::quat rotation{};
		glm::vec3 scale{};
		glm::mat4 transform{};
	};
}

