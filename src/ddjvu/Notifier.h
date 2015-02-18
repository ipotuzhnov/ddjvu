#ifndef DDJVU_NOTIFIER_H
#define DDJVU_NOTIFIER_H

#pragma once

#include <memory>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>

namespace ddjvu
{

	enum message_map { WINDOW, DOCUMENT, PAGE };
	enum message_window { CLOSE, UPDATE };
	enum message_document { INFO, CREATE, MESSAGE };
	enum message_page { ABBORTED, DECODED, RENDERED };

	class Notifier
	{
	public:
		Notifier(int notifierType) {
			int count = notifierType == message_map::WINDOW ? 2 : 3;
			for (int i = 0; i < count; i++)
				messages_.push_back(false);
		}

		~Notifier() {
			std::lock_guard<std::mutex> lck(mtx_);

			messages_.clear();
		}

		bool check(int message) {
			std::lock_guard<std::mutex> lck(mtx_);

			return messages_[message];
		}

		void wait(int message = -1) {
			std::unique_lock<std::mutex> lck(mtx_);

			if (message > 0)
				if (messages_[message])
					return;

			cv_.wait(lck);
		}

		void waitFor(int milliseconds = 0, int message = -1) {
			std::unique_lock<std::mutex> lck(mtx_);

			if (message > 0)
				if (messages_[message])
					return;

			cv_.wait_for(lck, std::chrono::milliseconds(milliseconds));
		}

		void set(int message) {
			std::lock_guard<std::mutex> lck(mtx_);

			messages_[message] = true;

			cv_.notify_all();
		}

		void setAll() {
			std::lock_guard<std::mutex> lck(mtx_);

			for (auto it = messages_.begin(); it != messages_.end(); ++it)
				*it = true;

			cv_.notify_all();
		}

		void reset(int message) {
			std::lock_guard<std::mutex> lck(mtx_);

			messages_[message] = false;

			cv_.notify_all();
		}

		void resetAll() {
			std::lock_guard<std::mutex> lck(mtx_);

			for (auto it = messages_.begin(); it != messages_.end(); ++it)
				*it = false;

			cv_.notify_all();
		}

	private:
		std::vector<bool> messages_;
		std::condition_variable cv_;
		std::mutex mtx_;
	};

}

#endif // DDJVU_NOTIFIER_H
