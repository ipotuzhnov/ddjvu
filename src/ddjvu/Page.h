#ifndef DDJVU_PAGE_H
#define DDJVU_PAGE_H

#pragma once

#include <stdio.h>
#include <unistd.h>

#include <memory>
#include <thread>
#include <string>

#include "ddjvuapi.h"

#include "Notifier.h"
#include "IBmp.h"
#include "IBmpFactory.h"

namespace ddjvu
{

	template<class T>
	class Page
	{
	private:
		std::shared_ptr<IBmpFactory<T>> delegateBmpFactory_;

		ddjvu_document_t *document_;
		int pageNum_;
		int width_;
		int height_;
		std::shared_ptr<IBmp<T>> bitmap_;
		std::string id_;

		std::thread decodeThread_;

		bool failed;
		bool decoded;
		bool rendered;

		void error(std::string err) {
			err = "===================" + err + "===================";
			fprintf(stdout,"logmsg: %s\n",err.c_str());
		}

		void decodeThreadFunction_()
		{
			if (!document_)
				return;

			error("ThreadlessPage: Start decoding page " + std::to_string(pageNum_));
			ddjvu_page_t *page;
			/* Decode page */
			if (! (page = ddjvu_page_create_by_pageno(document_, pageNum_)))
				error("ThreadlessPage: Cannot access page " + std::to_string(pageNum_));
			while (! ddjvu_page_decoding_done(page)) {
				error("ThreadlessPage: Decoding page sleep for 100 milliseconds");
				usleep(100 * 1000);
			}
			if (ddjvu_page_decoding_error(page)) {
				error("ThreadlessPage: Cannot decode page " + std::to_string(pageNum_));
				return;
			}
			error("ThreadlessPage: Decoded page " + std::to_string(pageNum_));

			/* Render page */
			error("ThreadlessPage: Start rendering page " + std::to_string(pageNum_));

			ddjvu_page_type_t type = ddjvu_page_get_type(page);

			bool isBitonal = (type == DDJVU_PAGETYPE_BITONAL) ? true : false;

			// ddjvu_page_render 1st param
			// *it

			// ddjvu_page_render 2nd param
			ddjvu_render_mode_t mode = isBitonal ? DDJVU_RENDER_MASKONLY : DDJVU_RENDER_COLOR;//DDJVU_RENDER_BLACK;

			int width = width_ ? width_ : ddjvu_page_get_width(page);
			int height = height_ ? height_ : ddjvu_page_get_height(page);

			// ddjvu_page_render 3rd param
			ddjvu_rect_t pageRect = { 0, 0, static_cast<unsigned int>(width), static_cast<unsigned int>(height) };

			// ddjvu_page_render 4th param
			ddjvu_rect_t rendRect = { 0, 0, static_cast<unsigned int>(width), static_cast<unsigned int>(height) };

			// ddjvu_page_render 5th param
			ddjvu_format_t* format;
			ddjvu_format_style_t formatStyle = isBitonal ? DDJVU_FORMAT_GREY8 : DDJVU_FORMAT_BGR24;

			format = ddjvu_format_create(formatStyle, 0, NULL);
			ddjvu_format_set_row_order(format, TRUE);

			// ddjvu_page_render 6th param
			int rowSize = ((width * (isBitonal ? 1 : 3) + 3) / 4) * 4;

			// ddjvu_page_render 7th param
			char *imageBuffer = new char[rowSize * height];
			memset(imageBuffer, 0, rowSize * height);

			if (ddjvu_page_render(page, mode, &pageRect, &rendRect, format, rowSize, imageBuffer)) {
				// Render HBITMAP
				int bitsPixel = isBitonal ? 1 : 3;
				int colors = isBitonal ? 256 : 0;

				bitmap_ = delegateBmpFactory_->createBmp(bitsPixel, colors, width, height, rowSize, imageBuffer);
			}

			delete[] imageBuffer;
			ddjvu_format_release(format);
		
			ddjvu_page_release(page);
			page = nullptr;

			rendered = true;

			error("ThreadlessPage: Rendered page " + std::to_string(pageNum_));
		}
	public:
		Page(ddjvu_document_t *document, std::shared_ptr<IBmpFactory<T>> delegateBmpFactory, std::string id = "", int pageNum = 0, int width = 0, int height = 0) {
			delegateBmpFactory_ = delegateBmpFactory;

			bitmap_ = std::shared_ptr<IBmp<T>>();

			document_ = document;
			pageNum_ = pageNum;
			width_ = width;
			height_ = height;
			id_ = id;

			failed = false;
			decoded = false;
			rendered = false;
		}

		~Page() {
			if (decodeThread_.joinable())
				decodeThread_.join();
		}

		void startInThread() {
			decodeThread_ = std::thread(&Page::decodeThreadFunction_, this);
		}

		void start() {
			decodeThreadFunction_();
		}

		bool ready() {
			if (rendered)
				return true;
			return false;
		}

		void wait() {
			while (!rendered) {
				error("ThreadlessPage: Wait renderer 100 milliseconds");
				usleep(100 * 1000);
			}
		}

		std::shared_ptr<IBmp<T>> getBitmap() {
			return bitmap_;
		}
	};

}

#endif // DDJVU_PAGE_H
