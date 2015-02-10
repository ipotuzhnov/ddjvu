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
		// Request page
		std::shared_ptr<Page<T>> getPage(std::string pageId, int pageNumber, int width, int height) {
			return document_->getPage(pageId, pageNumber, width, height);
		}
		// Remove page from decoding queue
		void removePage(std::string pageId) {
			return document_->removePage(pageId);
		}
		// Stop handling messages
		void stopMessageHandling() {
			document_->stopMessageHandling();
		}
	};

}

#endif // DDJVU_FILE_H
