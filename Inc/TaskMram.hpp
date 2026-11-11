#ifndef __TASKMRAM_HPP_
#define __TASKMRAM_HPP_

#include "Task.hpp"
#include "RegistrationManager.hpp"
#include "Transport.hpp"
#include "Mram.hpp"

#include "nunavut_assert.h"
#include "uavcan/file/Read_1_1.h"
#include "uavcan/file/Write_1_1.h"
#include "uavcan/file/Error_1_0.h"

#include <cstring>
#include <string_view>
#include <optional>

// -----------------------------------------------------------------------------
// Simple MRAM backend wrapper around a device with readBytes/writeBytes
// -----------------------------------------------------------------------------
template <typename Device>
class MramBackend
{
public:
	explicit MramBackend(Device &dev) : dev_(dev) {}

	bool read(uint32_t addr, void *buf, std::size_t len)
	{
		if (len > UINT16_MAX)
			return false;
		return dev_.readBytes(addr, buf, static_cast<uint16_t>(len));
	}

	bool write(uint32_t addr, const void *buf, std::size_t len)
	{
		if (len > UINT16_MAX)
			return false;
		return dev_.writeBytes(addr, buf, static_cast<uint16_t>(len));
	}

private:
	Device &dev_;
};

// -----------------------------------------------------------------------------
// Convert Cyphal path to string_view (zero-copy)
// -----------------------------------------------------------------------------
inline std::string_view convertPath(const uint8_t *data, std::size_t len)
{
	return std::string_view(reinterpret_cast<const char *>(data), len);
}

// -----------------------------------------------------------------------------
// Lookup MRAM offset by filename using MramRegistry + MramTraits
// -----------------------------------------------------------------------------
// Declaration only – implemented in mission code
template <typename Layout>
std::optional<uint32_t> mram_lookup_offset(std::string_view name);

// -----------------------------------------------------------------------------
// TaskMram: Cyphal file server backed by MRAM
// -----------------------------------------------------------------------------
template <typename Transport, typename Layout, typename... Adapters>
class TaskMram : public TaskForServer<CyphalBuffer8, Adapters...>
{
public:
	using Backend = MramBackend<Transport>;

	TaskMram() = delete;

	TaskMram(Backend &backend,
			 uint32_t interval,
			 uint32_t tick,
			 std::tuple<Adapters...> &adapters)
		: TaskForServer<CyphalBuffer8, Adapters...>(interval, tick, adapters), backend_(backend)
	{
	}

	void registerTask(RegistrationManager *manager,
					  std::shared_ptr<Task> task) override
	{
		manager->server(uavcan_file_Read_1_1_FIXED_PORT_ID_, task);
		manager->server(uavcan_file_Write_1_1_FIXED_PORT_ID_, task);
	}

	void unregisterTask(RegistrationManager *manager,
						std::shared_ptr<Task> task) override
	{
		manager->unserver(uavcan_file_Read_1_1_FIXED_PORT_ID_, task);
		manager->unserver(uavcan_file_Write_1_1_FIXED_PORT_ID_, task);
	}

	void handleTaskImpl() override
	{
		auto &buf = TaskForServer<CyphalBuffer8, Adapters...>::buffer_;
		if (buf.is_empty())
		{
			log(LOG_LEVEL_TRACE, "TaskMram: empty buffer\r\n");
			return;
		}

		const std::size_t count = buf.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			std::shared_ptr<CyphalTransfer> transfer = buf.pop();
			if (transfer->metadata.transfer_kind != CyphalTransferKindRequest)
				continue;

			switch (transfer->metadata.port_id)
			{
			case uavcan_file_Read_1_1_FIXED_PORT_ID_:
				handleRead(*transfer);
				break;
			case uavcan_file_Write_1_1_FIXED_PORT_ID_:
				handleWrite(*transfer);
				break;
			default:
				log(LOG_LEVEL_WARNING,
					"TaskMram: unknown port id %d\r\n",
					transfer->metadata.port_id);
				break;
			}
		}
	}

private:
	Backend &backend_;

protected:
	// -------------------------------------------------------------------------
	// WRITE
	// -------------------------------------------------------------------------
	void handleWrite(const CyphalTransfer &transfer)
	{
		uavcan_file_Write_Request_1_1 request{};
		std::size_t payload_size = transfer.payload_size;

		const int8_t deser =
			uavcan_file_Write_Request_1_1_deserialize_(
				&request,
				static_cast<const uint8_t *>(transfer.payload),
				&payload_size);

		uavcan_file_Write_Response_1_1 response{};

		if (deser < 0)
		{
			log(LOG_LEVEL_ERROR, "TaskMram: Write deserialization error\r\n");
			response._error.value = uavcan_file_Error_1_0_IO_ERROR;
		}
		else
		{
			std::string_view path =
				convertPath(request.path.path.elements,
							request.path.path.count);

			auto off = mram_lookup_offset<Layout>(path);

			if (!off)
			{
				log(LOG_LEVEL_WARNING,
					"TaskMram: Write unknown path '%.*s'\r\n",
					int(path.size()), path.data());
				response._error.value = uavcan_file_Error_1_0_NOT_FOUND;
			}
			else
			{
				const uint32_t addr = static_cast<uint32_t>(*off + request.offset);
				const bool ok = backend_.write(
					addr,
					request.data.value.elements,
					request.data.value.count);

				response._error.value =
					ok ? uavcan_file_Error_1_0_OK
					   : uavcan_file_Error_1_0_IO_ERROR;
			}
		}

		constexpr std::size_t PAYLOAD_SIZE =
			uavcan_file_Write_Response_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_;
		uint8_t payload[PAYLOAD_SIZE]{};

		this->publish(PAYLOAD_SIZE, payload, &response,
			reinterpret_cast<int8_t (*)(const void *const, uint8_t *const, size_t *const)>(uavcan_file_Write_Response_1_1_serialize_),
			uavcan_file_Write_1_1_FIXED_PORT_ID_, transfer.metadata.remote_node_id, transfer.metadata.transfer_id);

		log(LOG_LEVEL_INFO, "TaskMram: Write response sent\r\n");
	}

	// -------------------------------------------------------------------------
	// READ
	// -------------------------------------------------------------------------
	void handleRead(const CyphalTransfer &transfer)
	{
		uavcan_file_Read_Request_1_1 request{};
		std::size_t payload_size = transfer.payload_size;

		const int8_t deser =
			uavcan_file_Read_Request_1_1_deserialize_(
				&request,
				static_cast<const uint8_t *>(transfer.payload),
				&payload_size);

		uavcan_file_Read_Response_1_1 response{};

		if (deser < 0)
		{
			log(LOG_LEVEL_ERROR, "TaskMram: Read deserialization error\r\n");
			response._error.value = uavcan_file_Error_1_0_IO_ERROR;
		}
		else
		{
			std::string_view path =
				convertPath(request.path.path.elements,
							request.path.path.count);

			auto off = mram_lookup_offset<Layout>(path);

			if (!off)
			{
				log(LOG_LEVEL_WARNING,
					"TaskMram: Read unknown path '%.*s'\r\n",
					int(path.size()), path.data());
				response._error.value = uavcan_file_Error_1_0_NOT_FOUND;
			}
			else
			{
				const uint32_t base = *off;
				const uint32_t addr = static_cast<uint32_t>(base + request.offset);

				std::size_t bytes_to_read = uavcan_primitive_Unstructured_1_0_value_ARRAY_CAPACITY_;
				const bool ok = backend_.read(addr, response.data.value.elements, bytes_to_read);
				response.data.value.count = ok ? bytes_to_read : 0U;

				log(LOG_LEVEL_DEBUG,
					"TaskMram: Read path '%.*s', offset %u, size %zu\r\n",
					int(path.size()), path.data(),
					static_cast<unsigned>(request.offset),
					response.data.value.count);
			}
		}

		constexpr std::size_t PAYLOAD_SIZE =
			uavcan_file_Read_Response_1_1_SERIALIZATION_BUFFER_SIZE_BYTES_;
		uint8_t payload[PAYLOAD_SIZE]{};

		this->publish(PAYLOAD_SIZE, payload, &response,
			reinterpret_cast<int8_t (*)(const void *const, uint8_t *const, size_t *const)>(uavcan_file_Read_Response_1_1_serialize_),
			uavcan_file_Read_1_1_FIXED_PORT_ID_, transfer.metadata.remote_node_id, transfer.metadata.transfer_id);

		log(LOG_LEVEL_INFO, "TaskMram: Read response sent\r\n");
	}
};

#endif // __TASKMRAM_HPP_
