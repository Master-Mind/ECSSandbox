#pragma once
#include <string>
#include <unordered_map>

#include "TradComp.h"

namespace Trad
{
	class Entity
	{
	public:
		Entity(const std::string& name);
		const std::string& GetName() const { return _name; }
	private:
		std::string _name;
		std::unordered_map<size_t, Component *> _components;
	};
}
