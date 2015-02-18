#ifndef DDJVU_DOCUMENT_HANDLER_H
#define DDJVU_DOCUMENT_HANDLER_H

#pragma once

#include <stdio.h>

#include <memory>
#include <mutex>
#include <thread>
#include <map>
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
		std::shared_ptr<IBmpFactory<T>> delegateBmpFactory_;

		GP<DataPool> pool_;

		ddjvu_context_t *context_;
		ddjvu_document_t *document_;
		CallbackClosure *cc_;

		bool isDocumentValid_;

		std::shared_ptr<Notifier> windowNotifier_;
		std::shared_ptr<Notifier> documentNotifier_;

		std::thread messageHandleThread_;

		std::mutex pages_mtx_;
		std::map<std::string, std::shared_ptr<Page<T>>> pages_;

		void error(std::string err) {
			err = "===================" + err + "===================";
			//fprintf(stdout,"logmsg: %s\n",err.c_str());
		}
	public:
		Document(GP<DataPool> pool, std::shared_ptr<IBmpFactory<T>> delegateBmpFactory) {
			delegateBmpFactory_ = delegateBmpFactory;

			isDocumentValid_ = false;

			pool_ = pool;

			/* Create context and document */
			context_ = ddjvu_context_create(0);
			if (! context_)
				error("Cannot create djvu context.");
			/* Create message handling thread */
			messageHandleThread_ = std::thread(&Document::messageHandleThreadFunction_, this);
			/* Set callback function */
			windowNotifier_ = std::shared_ptr<Notifier>(new Notifier(message_map::WINDOW));
			documentNotifier_ = std::shared_ptr<Notifier>(new Notifier(message_map::DOCUMENT));
			cc_ = new CallbackClosure();
			cc_->windowNotifier = windowNotifier_;
			cc_->documentNotifier = documentNotifier_;

			ddjvu_message_set_callback(context_, ddjvuMessageCallback, cc_);
			/* Create document */
			document_ = ddjvu_document_create(context_, 0, FALSE);
			if (! document_)
				error("Cannot open djvu document.");

			while (! ddjvu_document_decoding_done(document_)) {
				error("Creating document sleep for 100 milliseconds");
				documentNotifier_->waitFor(100);
			}
			if (ddjvu_document_decoding_error(document_)) {
				error("Cannot decode document.");
			} else {
				isDocumentValid_ = true;
				error("Doc created successfully");
			}
		}

		~Document() {
			while (!pages_.empty()) {
				std::lock_guard<std::mutex> lck(pages_mtx_);
				std::shared_ptr<Page<T>> page = pages_.begin()->second;
				pages_.erase(pages_.begin()->first);
			}

			windowNotifier_->set(message_window::CLOSE);
			documentNotifier_->set(message_document::MESSAGE);
			if (messageHandleThread_.joinable())
				messageHandleThread_.join();

			if (document_)
				ddjvu_document_release(document_);
			if (context_)
				ddjvu_context_release(context_);

			delete cc_;
		}

		void stopMessageHandling() {
			windowNotifier_->set(message_window::CLOSE);
			documentNotifier_->set(message_document::MESSAGE);
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

		std::shared_ptr<Page<T>> getPage(std::string pageId, int pageNumber, int width, int height) {
			std::lock_guard<std::mutex> lck(pages_mtx_);
			pages_[pageId] = std::make_shared<Page<T>>(document_, delegateBmpFactory_, pageId, pageNumber, width, height);
			return pages_[pageId];
		}

		std::shared_ptr<Page<T>> findPage(ddjvu_page_t *page) {
			std::lock_guard<std::mutex> lck(pages_mtx_);
			std::shared_ptr<Page<T>> res;
			for (auto it = pages_.begin(); it != pages_.end(); ++it) {
				if (page == it->second->getDdjvuPage())
					return it->second;
			}
			return res;
		}

		void removePage(std::string pageId) {
			std::lock_guard<std::mutex> lck(pages_mtx_);
			pages_.erase(pageId);
		}

		GP<DataPool> getPool() {
			return pool_;
		}

		ddjvu_context_t * getContext() {
			return context_;
		}

		ddjvu_document_t * getDocument() {
			return document_;
		}
	private:
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
			error("Hello from message handling thread");

			int streamid = 0;
			int i = 0;

			while (!windowNotifier_->check(message_window::CLOSE)) {

				bool isEOF = false;
				const ddjvu_message_t *msg;
				ddjvu_document_t *document = nullptr;
				ddjvu_page_t *page = nullptr;

				// Don't wait
				//ddjvu_message_wait(context_);
				while ((msg = ddjvu_message_peek(context_))) {
					//error("peeked new message from context");
					switch( msg->m_any.tag ) {
					case DDJVU_ERROR:
					{
						error("DDJVU_ERROR");
						error(msg->m_error.message);
						error(msg->m_error.filename);
						error(std::to_string(msg->m_error.lineno));
						error(msg->m_error.function);
					}
					break;
					case DDJVU_INFO:
						break;
					case DDJVU_NEWSTREAM:
						error("DDJVU_NEWSTREAM: load stream");
						document = msg->m_any.document;
						if (document == nullptr)
							error("msg->m_any.document is nullprt");
						else {
							streamid = msg->m_newstream.streamid;
							char buffer[512];

							while (i < pool_->get_length() && !isEOF) {
								try {
									int size = pool_->get_data(buffer, i, 512);
									i += size;
									ddjvu_stream_write(document, streamid, buffer, size);
								} catch (...) {
									// Process error
									isEOF = pool_->is_eof();
								}
							}
							ddjvu_stream_close(document, streamid, 0);
						}
						break;
					case DDJVU_DOCINFO:
						error("Got doc info");
						documentNotifier_->set(message_document::INFO);
						break;
					case DDJVU_PAGEINFO:
						page = msg->m_any.page;
						if (page == nullptr)
							error("msg->m_any.page is nullprt");
						else {
							if (ddjvu_page_decoding_status(msg->m_any.page) == DDJVU_JOB_OK) {
								auto res = findPage(page);
								res->getPageNotifier()->set(message_page::DECODED);
							}
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
				// Wait 100 milliseconds
				error("No messages wait for 100 milliseconds");
				documentNotifier_->waitFor(100);
			}
		}

	};

}

#endif // DDJVU_DOCUMENT_HANDLER_H
