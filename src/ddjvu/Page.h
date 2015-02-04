#ifndef DDJVU_PAGE_H
#define DDJVU_PAGE_H

#pragma once

#include <memory>
#include <mutex>
#include <thread>
#include <string>

#include "ddjvuapi.h"

#include "Notifier.h"
#include "IBmp.h"
#include "IBmpFactory.h"

//@TODO remove it later
#include "src/helpers/safe_instance.h"
#include "src/helpers/message_helper.h"

namespace ddjvu
{

	template<class T>
	class Page
	{
	private:
		std::shared_ptr<SafeInstance> safeInstance_;

		std::shared_ptr<IBmpFactory<T>> delegateBmpFactory_;

		ddjvu_document_t *document_;
		ddjvu_page_t *page_;
		int pageNum_;
		int width_;
		int height_;
		int view_;
		std::shared_ptr<IBmp<T>> bitmap_;
		bool isAborted_;
		std::string id_;

		std::shared_ptr<Notifier> pageNotifier_;
		std::shared_ptr<Notifier> windowNotifier_;

		std::thread decodeThread_;

		std::mutex mtx_;

		void decodeThreadFunction_()
		{
			PostLogMessage(safeInstance_, "LOG: before Page::Thread()");
			while (!pageNotifier_->check(message_page::DECODED) && !pageNotifier_->check(message_page::ABBORTED)) {
				if (ddjvu_page_decoding_status(page_) == DDJVU_JOB_OK) {
					pageNotifier_->set(message_page::DECODED);
				}
				if (ddjvu_page_decoding_status(page_) == DDJVU_JOB_FAILED) {
					pageNotifier_->set(message_page::ABBORTED);
				}
				pageNotifier_->waitFor(-1, 1);
			}

			if (pageNotifier_->check(message_page::DECODED) && !pageNotifier_->check(message_page::ABBORTED)) {

				ddjvu_page_type_t type = ddjvu_page_get_type(page_);

				bool isBitonal = (type == DDJVU_PAGETYPE_BITONAL) ? true : false;

				// ddjvu_page_render 1st param
				// *it

				// ddjvu_page_render 2nd param
				ddjvu_render_mode_t mode = isBitonal ? DDJVU_RENDER_MASKONLY : DDJVU_RENDER_COLOR;//DDJVU_RENDER_BLACK;

				int width = width_ ? width_ : ddjvu_page_get_width(page_);
				int height = height_ ? height_ : ddjvu_page_get_height(page_);

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

				if (ddjvu_page_render(page_, mode, &pageRect, &rendRect, format, rowSize, imageBuffer)) {
					// Render HBITMAP
					int bitsPixel = isBitonal ? 1 : 3;
					int colors = isBitonal ? 256 : 0;

					bitmap_ = delegateBmpFactory_->createBmp(bitsPixel, colors, width, height, rowSize, imageBuffer);
				}

				delete[] imageBuffer;
				ddjvu_format_release(format);
			}

			{
				std::lock_guard<std::mutex> lck(mtx_);
				ddjvu_page_release(page_);
				page_ = 0;
			}

			pageNotifier_->set(message_page::RENDERED);

			windowNotifier_->set(message_window::UPDATE);
			PostLogMessage(safeInstance_, "LOG: after Page::Thread()");
		}
	public:
		Page(ddjvu_document_t *document, std::shared_ptr<IBmpFactory<T>> delegateBmpFactory, std::shared_ptr<Notifier> windowNotifier, int pageNum = 0, int width = 0, int height = 0, bool wait = false, int view = 0) {
			pageNotifier_ = std::shared_ptr<Notifier>(new Notifier(message_map::PAGE));
			windowNotifier_ = windowNotifier;

			delegateBmpFactory_ = delegateBmpFactory;

			bitmap_ = std::shared_ptr<IBmp<T>>();
			isAborted_ = false;

			document_ = document;
			pageNum_ = pageNum;
			width_ = width;
			height_ = height;
			view_ = view;

			page_ = ddjvu_page_create_by_pageno(document_, pageNum_);
			//ddjvu_page_set_user_data(page_, this);

			decodeThread_ = std::thread(&Page::decodeThreadFunction_, this);

			if (wait) {
				while (!pageNotifier_->check(message_page::RENDERED) && !pageNotifier_->check(message_page::ABBORTED))
					pageNotifier_->wait();
			}
		}

		Page(std::shared_ptr<SafeInstance> safeInstance, ddjvu_document_t *document, std::shared_ptr<IBmpFactory<T>> delegateBmpFactory, std::shared_ptr<Notifier> windowNotifier, int pageNum = 0, int width = 0, int height = 0, bool wait = false, std::string id = "") {
			safeInstance_ = safeInstance;

			PostLogMessage(safeInstance_, "LOG: before Page::Page()");

			pageNotifier_ = std::shared_ptr<Notifier>(new Notifier(message_map::PAGE));
			windowNotifier_ = windowNotifier;

			delegateBmpFactory_ = delegateBmpFactory;

			bitmap_ = std::shared_ptr<IBmp<T>>();
			isAborted_ = false;

			document_ = document;
			pageNum_ = pageNum;
			width_ = width;
			height_ = height;
			id_ = id;

			page_ = ddjvu_page_create_by_pageno(document_, pageNum_);
			//ddjvu_page_set_user_data(page_, this);

			decodeThread_ = std::thread(&Page::decodeThreadFunction_, this);
			PostLogMessage(safeInstance_, "LOG: after Page::Page()");
		}

		~Page() {
			page_ = 0;
			pageNotifier_->set(message_page::ABBORTED);
			PostLogMessage(safeInstance_, "LOG: before ~Page::Page()");
			if (decodeThread_.joinable())
				decodeThread_.join();
			else
				PostLogMessage(safeInstance_, "LOG: Page::Thread is not joinable");
			PostLogMessage(safeInstance_, "LOG: after ~Page::Page()");
		}

		/*
		Return:
			true if page is rendered.
			false if page is aborted.
		*/
		bool isBitmapReady() {
			while (!pageNotifier_->check(message_page::RENDERED) && !pageNotifier_->check(message_page::ABBORTED))
				pageNotifier_->wait();
			if (pageNotifier_->check(message_page::ABBORTED))
				return false;
			return true;
		}

		bool abort() {
			pageNotifier_->set(message_page::ABBORTED);

			if (pageNotifier_->check(message_page::DECODED)) {
				if (pageNotifier_->check(message_page::RENDERED)) {
					return true;		
				} else {
					return false;
				}
			} else {
				ddjvu_status_t r = DDJVU_JOB_STOPPED;
				{
					std::lock_guard<std::mutex> lck(mtx_);
					if (page_)
						r = ddjvu_job_status(ddjvu_page_job(page_));
				}
				if (r == DDJVU_JOB_STARTED) {
					{
						std::lock_guard<std::mutex> lck(mtx_);
						if (page_)
							ddjvu_job_stop(ddjvu_page_job(page_));
					}
					return false;
				} else {
					while (!pageNotifier_->check(message_page::RENDERED))
						pageNotifier_->wait(message_page::RENDERED);
					return true;
				}
			}
		}

		void setBitmap(std::shared_ptr<IBmp<T>> bitmap) {
			bitmap_ = bitmap;
		}

		void setPage(ddjvu_page_t *page) {
			std::lock_guard<std::mutex> lck(mtx_);

			page_ = page;
		}

		ddjvu_document_t *getDocument() {
			return document_;
		}

		ddjvu_page_t *getPage() {
			std::lock_guard<std::mutex> lck(mtx_);

			ddjvu_page_t *page = page_;

			return page;
		}

		int getPageNum() {
			return pageNum_;
		}

		int getWidth() {
			return width_;
		}

		int getHeight() {
			return height_;
		}

		int getView() {
			return view_;
		}

		std::string getId() {
			return id_;
		}

		std::shared_ptr<IBmp<T>> getBitmap() {
			return bitmap_;
		}

		std::shared_ptr<Notifier> getPageNotifier() {
			return pageNotifier_;
		}
	};

}

#endif // DDJVU_PAGE_H
