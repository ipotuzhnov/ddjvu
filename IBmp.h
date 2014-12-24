#pragma once

#include <memory>

namespace ddjvu {

	template <class T>
	class IBmp {
	public:
		virtual void setBmp(std::shared_ptr<T> bmp) = 0;
		virtual std::shared_ptr<T> getBmp() = 0;
	};

}
