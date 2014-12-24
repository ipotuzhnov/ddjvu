#pragma once

#include "IBmp.h"

namespace ddjvu {

	template <class T>
	class IBmpFactory {
	public:
		virtual std::shared_ptr<IBmp<T>> createBmp(int bitsPixel, int colors, int width, int height, int rowSize, char * imageBuffer) = 0;
	};

}
