#ifndef DDJVU_DOCUMENT_HANDLER_H
#define DDJVU_DOCUMENT_HANDLER_H

#pragma once

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <locale>
#include <codecvt>
#include <string>

#include "DataPool.h"
#include "ddjvuapi.h"
#include "miniexp.h"

#include "Notifier.h"
#include "Text.h"
#include "Page.h"

#include "IBmpFactory.h"
#include "IBmp.h"



namespace ddjvu
{

	class CallbackClosure {
	public:
		std::shared_ptr<Notifier> windowNotifier;
		std::shared_ptr<Notifier> documentNotifier;
	};

	static void ddjvuMessageCallback(ddjvu_context_t *context, void *closure)
	{
		CallbackClosure *cc = (CallbackClosure*)closure;
		if (!cc->windowNotifier->check(message_window::CLOSE))
			cc->documentNotifier->set(message_document::MESSAGE);
	}

	template <class T>
	class Document
	{
	private:
		//@TODO remove
		std::shared_ptr<SafeInstance> safeInstance_;

		std::shared_ptr<IBmpFactory<T>> delegateBmpFactory_;

		GP<DataPool> pool_;

		ddjvu_context_t *context_;
		ddjvu_document_t *document_;
		CallbackClosure *cc_;

		std::vector<std::shared_ptr<Page<T>>> pages_;

		bool isDocumentValid_;

		std::shared_ptr<Notifier> windowNotifier_;
		std::shared_ptr<Notifier> documentNotifier_;

		std::thread messageHandleThread_;

		std::mutex mtx_;

	public:
		Document(GP<DataPool> pool, std::shared_ptr<IBmpFactory<T>> delegateBmpFactory, std::shared_ptr<SafeInstance> safeInstance) {
			delegateBmpFactory_ = delegateBmpFactory;
			safeInstance_ = safeInstance;

			isDocumentValid_ = false;
			pool_ = pool;
			context_ = ddjvu_context_create(0);

			windowNotifier_ = std::shared_ptr<Notifier>(new Notifier(message_map::WINDOW));
			documentNotifier_ = std::shared_ptr<Notifier>(new Notifier(message_map::DOCUMENT));

			cc_ = new CallbackClosure();
			cc_->windowNotifier = windowNotifier_;
			cc_->documentNotifier = documentNotifier_;

			messageHandleThread_ = std::thread(&Document<T>::messageHandleThreadFunction_, this);

			ddjvu_message_set_callback(context_, ddjvuMessageCallback, cc_);
			document_ = ddjvu_document_create(context_, 0, FALSE);

			documentNotifier_->set(message_document::CREATE);

			// Waiting for getting info
			while (!documentNotifier_->check(message_document::INFO))
				documentNotifier_->wait();

			if (ddjvu_document_decoding_status(document_) != DDJVU_JOB_FAILED)
				isDocumentValid_ = true;
		}

		~Document() {
			while (!pages_.empty()) {
				std::shared_ptr<Page<T>> page = pages_.back();
				pages_.pop_back();
			}

			windowNotifier_->set(message_window::CLOSE);
			documentNotifier_->set(message_document::MESSAGE);
			messageHandleThread_.join();

			ddjvu_document_release(document_);
			ddjvu_context_release(context_);

			delete cc_;
		}

		void abortPageDecode(int pageNum, int width, int height, int view) {
			std::lock_guard<std::mutex> lck(mtx_);
			for (auto it = pages_.begin(); it != pages_.end(); ++it) {
				if ((*it)->getPageNum() == pageNum &&
					(*it)->getWidth() == width &&
					(*it)->getHeight() == height &&
					(*it)->getView() == view) {
						if ((*it)->abort()) {
							auto page = *it;
							pages_.erase(it);
							return;
						}
				}
			}
		}

		void abortPageDecode(std::string id) {
			std::lock_guard<std::mutex> lck(mtx_);
			for (auto it = pages_.begin(); it != pages_.end(); ++it) {
				if ((*it)->getId() == id) {
						if ((*it)->abort()) {
							auto page = *it;
							pages_.erase(it);
							return;
						}
				}
			}
		}

		bool isDocumentValid() {
			return isDocumentValid_;
		}

		int getPageNum() {
			return ddjvu_document_get_pagenum(document_);
		}

		ddjvu_pageinfo_t getPageInfo(int pageNum) {
			ddjvu_status_t r;
			ddjvu_pageinfo_t info;

			do {
				r = ddjvu_document_get_pageinfo(document_, pageNum, &info);
			} while (r != DDJVU_JOB_OK);

			return info;
		}

		std::vector<Text> getPageText(int pageNum) {
			ddjvu_pageinfo_t info = getPageInfo(pageNum);

			std::vector<Text> words;

			miniexp_t exp = miniexp_nil;
			while ((exp = ddjvu_document_get_pagetext(document_, pageNum, 0)) == miniexp_dummy)
				;
			if (exp != miniexp_nil)
				getWords(exp, words, info.width, info.height);

			return words;
		}

		std::shared_ptr<IBmp<T>> getPageBitmap(int pageNum = 0, int width = 0, int height = 0, bool wait = false, int view = 0) {
			std::shared_ptr<Page<T>> page = findPage(pageNum, width, height, view);
			if (page) {
				if (wait) {
					auto pageNotifier = page->getPageNotifier();
					while (!pageNotifier->check(message_page::RENDERED))
						pageNotifier->wait();
				}
				return page->getBitmap();
			} else {
				auto page = std::shared_ptr<Page<T>>(new Page<T>(document_, delegateBmpFactory_, windowNotifier_, pageNum, width, height, wait, view));
				{
					std::lock_guard<std::mutex> lck(mtx_);
					pages_.push_back(page);
				}			
				if (wait)
					return page->getBitmap();
			}
			return std::shared_ptr<IBmp<T>>();
		}

		std::shared_ptr<IBmp<T>> getPageBitmap(std::shared_ptr<SafeInstance> safeInstance, int pageNum = 0, int width = 0, int height = 0, bool wait = false, std::string id = "") {
			std::shared_ptr<Page<T>> page = findPage(id);
			if (page) {
				/*
				if (wait) {
					auto pageNotifier = page->getPageNotifier();
					while (!pageNotifier->check(message_page::RENDERED))
						pageNotifier->wait();
				}
				*/
				return page->getBitmap();
			} else {
				auto page = std::shared_ptr<Page<T>>(new Page<T>(safeInstance, document_, delegateBmpFactory_, windowNotifier_, pageNum, width, height, wait, id));
				{
					std::lock_guard<std::mutex> lck(mtx_);
					pages_.push_back(page);
				}
				/*
				if (wait)
					page->wait();
					return page->getBitmap();
				*/
			}
			return std::shared_ptr<IBmp<T>>();
		}

		GP<DataPool> getPool()
		{
			return pool_;
		}

		ddjvu_context_t *getContext() {
			return context_;
		}

		ddjvu_document_t *getDocument() {
			return document_;
		}

		std::vector<int> getRenderedViews() {
			std::lock_guard<std::mutex> lck(mtx_);

			std::vector<int> views;
			for (auto it = pages_.begin(); it != pages_.end(); ++it) {
				if ((*it)->getPageNotifier()->check(message_page::RENDERED))
					views.push_back((*it)->getView());
			}

			return views;
		}

		bool isBitmapReady(std::string id) {
			std::shared_ptr<Page<T>> page = findPage(id);
			if (page) {
				return page->isBitmapReady();
			}
			return false;
		}

		std::shared_ptr<Notifier> getWindowNotifier() {
			return windowNotifier_;
		}

		std::shared_ptr<Notifier> getDocumentNotifier() {
			return documentNotifier_;
		}
	private:
		std::shared_ptr<Page<T>> findPage(int pageNum, int width, int height, int view) {
			std::lock_guard<std::mutex> lck(mtx_);

			std::shared_ptr<Page<T>> page;
			for (auto it = pages_.begin(); it != pages_.end(); ++it) {
				if ((*it)->getPageNum() == pageNum &&
					(*it)->getWidth() == width &&
					(*it)->getHeight() == height &&
					(*it)->getView() == view)
					page = *it;
			}

			return page;
		}

		std::shared_ptr<Page<T>> findPage(std::string id) {
			std::lock_guard<std::mutex> lck(mtx_);

			std::shared_ptr<Page<T>> page;
			for (auto it = pages_.begin(); it != pages_.end(); ++it) {
				if ((*it)->getId() == id)
					page = *it;
			}

			return page;
		}

		std::shared_ptr<Page<T>> findPage(ddjvu_page_t *ddjvu_page) {
			std::lock_guard<std::mutex> lck(mtx_);

			std::shared_ptr<Page<T>> page;
			for (auto it = pages_.begin(); it != pages_.end(); ++it) {
				if ((*it)->getPage() == ddjvu_page)
					page = *it;
			}

			return page;
		}

		void getWords(miniexp_t exp, std::vector<Text> &words, int width, int height) {
			if(!miniexp_consp(exp)) {
				// Not a list or empty list
				return;
			}

			const char* expName = miniexp_to_name(miniexp_nth(0, exp));
			if (!expName) {
				// Not a symbol
				return;
			}

			if (strcmp(expName, "word") == 0) {
				int n = miniexp_length(exp);
				if (n == 6) {
					if (miniexp_numberp(miniexp_nth(1, exp)) &&
						miniexp_numberp(miniexp_nth(2, exp)) &&
						miniexp_numberp(miniexp_nth(3, exp)) &&
						miniexp_numberp(miniexp_nth(4, exp)) &&
						miniexp_stringp(miniexp_nth(5, exp))) {
							int x1 = miniexp_to_int(miniexp_nth(1, exp));
							int y1 = height - miniexp_to_int(miniexp_nth(4, exp));
							int x2 = miniexp_to_int(miniexp_nth(3, exp));
							int y2 = height - miniexp_to_int(miniexp_nth(2, exp));

							std::string s(miniexp_to_str(miniexp_nth(5, exp)));
							/* Old style windows only conversion
							int len;
							int slength = (int)s.length() + 1;
							len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), slength, 0, 0); 
							wchar_t* buf = new wchar_t[len];
							MultiByteToWideChar(CP_UTF8, 0, s.c_str(), slength, buf, len);
							std::wstring word(buf);
							delete[] buf;
							*/

							std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
							std::wstring word = converter.from_bytes(s);

							Rectangle r = { x1, y1, x2, y2 };
							words.push_back(Text(word.c_str(), r));
					} else {
						// Wrong type for s-expression
					}
				} else {
					// Wrong list size for word annotation!
				}
			} else {
				// non-word: recursing
				int n = miniexp_length(exp);
				miniexp_t sub;
				for (int i = 1; i < n; i++) {
					sub = miniexp_nth(i,exp);
					getWords(sub, words, width, height);
				}
			}
		}

		void messageHandleThreadFunction_() {
			// Wait for document creation
			while (!documentNotifier_->check(message_document::CREATE))
				documentNotifier_->wait();

			int streamid = 0;
			int i = 0;	

			while (!windowNotifier_->check(message_window::CLOSE)) {
				if (!documentNotifier_->check(message_document::MESSAGE))
					documentNotifier_->wait();

				if (documentNotifier_->check(message_document::MESSAGE) && !windowNotifier_->check(message_window::CLOSE)) {
					bool isEOF = false;
					const ddjvu_message_t *msg;

					ddjvu_message_wait(context_);
					while ((msg = ddjvu_message_peek(context_)))
					{
						switch( msg->m_any.tag )
						{
						case DDJVU_ERROR:
							/*
							{
							fprintf(stderr,"ddjvu: %s\n", msg->m_error.message);
							if (msg->m_error.filename)
								fprintf(stderr,"ddjvu: '%s:%d'\n", 
								msg->m_error.filename, msg->m_error.lineno);
							}
							*/
							break;
						case DDJVU_INFO:
							break;
						case DDJVU_NEWSTREAM:
							streamid = msg->m_newstream.streamid;
							char buffer[512];

							while (i < pool_->get_length() && !isEOF) {
								try {
									int size = pool_->get_data(buffer, i, 512);
									i += size;
									ddjvu_stream_write(document_, streamid, buffer, size);
								} catch (...) {
									// Process error
									isEOF = pool_->is_eof();								
								}

							}
							ddjvu_stream_close(document_, streamid, 0);
							break;
						case DDJVU_DOCINFO:
							documentNotifier_->set(message_document::INFO);
							break;
						case DDJVU_PAGEINFO:
							if (msg->m_any.page) {
								PostLogMessage(safeInstance_, "LOG: before Document::findPage(ddjvu_page)");
								ddjvu_page_t *ddjvu_page = msg->m_any.page;
								if (ddjvu_page == nullptr)
									PostLogMessage(safeInstance_, "LOG: ddjvu_page == nullptr");
								auto page = findPage(ddjvu_page);
								if (page)
									PostLogMessage(safeInstance_, "LOG: found ddjvu_page");
								else
									PostLogMessage(safeInstance_, "LOG: didn't find ddjvu_page");

								PostLogMessage(safeInstance_, "LOG: after Document::findPage(ddjvu_page)");

								PostLogMessage(safeInstance_, "LOG: before Document::DDJVU_PAGEINFO");
								//Page<T> *page = (Page<T> *)ddjvu_page_get_user_data(msg->m_any.page);
								std::string out = "LOG: ";
								if (ddjvu_page_decoding_status(msg->m_any.page) == DDJVU_JOB_NOTSTARTED)
									out += " DDJVU_JOB_NOTSTARTED\n";
								if (ddjvu_page_decoding_status(msg->m_any.page) == DDJVU_JOB_STARTED)
									out += " DDJVU_JOB_STARTED\n";
								if (ddjvu_page_decoding_status(msg->m_any.page) == DDJVU_JOB_OK)
									out += "DDJVU_JOB_OK ";
								if (ddjvu_page_decoding_status(msg->m_any.page) == DDJVU_JOB_FAILED) {
									//Page<T> *page = (Page<T> *)ddjvu_page_get_user_data(msg->m_any.page);
									if (page)
										page->getPageNotifier()->set(message_page::ABBORTED);
									out += page->getId();
									out += " DDJVU_JOB_FAILED\n";
								}
								if (ddjvu_page_decoding_status(msg->m_any.page) == DDJVU_JOB_STOPPED)
									out += " DDJVU_JOB_STOPPED\n";
								//OutputDebugStringA(out.c_str());
								if (ddjvu_page_decoding_status(msg->m_any.page) == DDJVU_JOB_OK) {
									//Page<T> *page = (Page<T> *)ddjvu_page_get_user_data(msg->m_any.page);
									if (page)
										page->getPageNotifier()->set(message_page::DECODED);
								}
								PostLogMessage(safeInstance_, out);
								PostLogMessage(safeInstance_, "LOG: after Document::DDJVU_PAGEINFO");
							}
							break;
						case DDJVU_RELAYOUT:
							break;
						case DDJVU_REDISPLAY:
							break;
						case DDJVU_CHUNK:
							break;
						case DDJVU_THUMBNAIL:
							break;
						case DDJVU_PROGRESS:
							break;
						default: ;
						}

						ddjvu_message_pop(context_);
					}
					documentNotifier_->reset(message_document::MESSAGE);
				}
			}
		}

	};

}

#endif // DDJVU_DOCUMENT_HANDLER_H
