#ifndef DDJVU_FILE_H
#define DDJVU_FILE_H

#pragma once

#include <memory>
#include <vector>

#include "DataPool.h"
#include "ddjvuapi.h"

#include "Notifier.h"
#include "IBmp.h"
#include "Text.h"
#include "Document.h"

#include "IBmpFactory.h"

/*	Files "File.h" and "File.cpp" implements class ddjvu::File

ddjvu::File is easy to use class that provides access to DjVu document data.

To use this class you should always implement ddjvu::Bitmap class
*/

namespace ddjvu {	

	template <class T>
	class File {
	private:
		std::shared_ptr<Document<T>> document_;
	public:
		File(GP<DataPool> pool, std::shared_ptr<IBmpFactory<T>> delegateBmpFactory) {
			document_ = std::shared_ptr<Document<T>>(new Document<T>(pool, delegateBmpFactory));
		}
		~File() {}

		// Check if the document was created successfully
		bool isDocumentValid() {
			return document_->isDocumentValid();
		}
		// Get number of pages in document
		int getPageNum() {
			return document_->getPageNum();
		}
		// Get page info
		ddjvu_pageinfo_t getPageInfo(int pageNum) {
			return document_->getPageInfo(pageNum);
		}
		// Get page text
		std::vector<Text> getPageText(int pageNum) {
			return document_->getPageText(pageNum);
		}
		// Trying to get page image.
		// Function supports synchronous and asynchronous mode.
		//     In synchronous mode waits until page decoded.
		//     In asynchronous mode function returns immediately.
		// You should use ddjvu::Notifier to handle page decode completion.
		std::shared_ptr<IBmp<T>> getPageBitmap(int pageNum = 0, int width = 0, int height = 0, bool wait = false, int view = 0) {
			return document_->getPageBitmap(pageNum, width, height, wait, view);
		}
		std::shared_ptr<IBmp<T>> getPageBitmap(int pageNum = 0, int width = 0, int height = 0, bool wait = false, std::string id = "") {
			return document_->getPageBitmap(pageNum, width, height, wait, id);
		}
		//
		// Checks if page is rendered.
		//    true if page is rendered.
		//    false if page is aborted.
		bool isBitmapReady(std::string id = "") {
			return document_->isBitmapReady(id);
		}
		// Fuction return ddjvu::Notifier
		std::shared_ptr<Notifier> getWindowNotifier() {
			return document_->getWindowNotifier();
		}
		// Get view IDs that are already rendered
		std::vector<int> getRenderedViews() {
			return document_->getRenderedViews();
		}
		// Stop page decoding
		void abortPageDecode(int pageNum, int width, int height, int view) {
			document_->abortPageDecode(pageNum, width, height, view);
		}

		void abortPageDecode(std::string id) {
			document_->abortPageDecode(id);
		}
	};

}

#endif // DDJVU_FILE_H
