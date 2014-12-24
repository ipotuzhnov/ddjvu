#ifndef DDJVU_TEXT_H
#define DDJVU_TEXT_H

#pragma once

#include <string>

#include "Rectangle.h"

namespace ddjvu
{

	class Text
	{
	public:
		Text(std::wstring word, Rectangle rect) {
			word_ = word;
			rect_ = rect;
		}

		std::wstring getWord() {
			return word_;
		}

		Rectangle getRect() {
			return rect_;
		}

	private:
		std::wstring word_;
		Rectangle rect_;
	};

}

#endif // DDJVU_TEXT_H
