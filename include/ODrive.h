#pragma once

#include "libusbcpp.h"
#include "CRC.h"
#include "Endpoint.h"
#include "libusbcpp.h"
#include <stdio.h>

#include "json.hpp"

#define ODRIVE_VENDOR_ID 0x1209
#define ODRIVE_PRODUCT_ID 0x0D32

#define ODRIVE_USB_INTERFACE 2
#define ODRIVE_USB_READ_ENDPOINT (uint16_t)0x83
#define ODRIVE_USB_WRITE_ENDPOINT (uint16_t)0x03

#define ODRIVE_TIMEOUT 0.5		// Read/Write timeout in seconds

typedef std::vector<uint8_t> buffer_t;
using njson = nlohmann::json;

static inline std::string toUpper(const std::string& str) {
	std::string s;
	for (char c : str) 
		s += toupper(c);

	return s;
}

static inline std::string replace(const std::string& str, char find, char replace) {
	std::string s;
	for (char c : str)
		s += (c == find) ? replace : c;

	return s;
}

class ODrive {
public:

	bool connected = true;
	bool loaded = false;
	uint16_t jsonCRC = 0x00;
	uint64_t serialNumber = 0;
	std::string json;
	std::vector<Endpoint> endpoints;
	std::vector<BasicEndpoint> cachedEndpoints;

	bool error = false;
	int32_t axis0Error = 0x00;
	int32_t axis1Error = 0x00;
	int32_t motor0Error = 0x00;
	int32_t motor1Error = 0x00;
	int32_t encoder0Error = 0x00;
	int32_t encoder1Error = 0x00;
	int32_t controller0Error = 0x00;
	int32_t controller1Error = 0x00;

	ODrive(libusbcpp::device device) : device(device) {
		if (!device) {
			throw std::runtime_error("ODrive device is nullptr!");
		}
		if (!device->claimInterface(ODRIVE_USB_INTERFACE)) {
			throw std::runtime_error("Cannot claim USB interface");
		}
		load(999);
	}

	template<typename T>
	bool read(uint16_t endpoint, T* value_ptr) {

		if (!loaded || !connected)
			return false;

		std::lock_guard<std::mutex> lock(transferMutex);
		uint16_t sequence = sendReadRequest(endpoint, sizeof(T), {}, jsonCRC);

		double start = Battery::GetRuntime();
		while (Battery::GetRuntime() < start + ODRIVE_TIMEOUT) {
			auto& response = getResponse(sizeof(T));
			if ((response.first & 0b0111111111111111) == sequence && response.second.size() == sizeof(T)) {
				memcpy(value_ptr, &response.second[0], sizeof(T));
				return true;
			}
		}
		LOG_WARN("Timeout: Failed to read endpoint {}", endpoint);
		LOG_WARN("Requested data was: Endpoint: {}, type {}, no payload, crc=0x{:04X}", endpoint, typeid(T).name(), jsonCRC);
		return false;
	}

	template<typename T>
	bool read(const std::string& identifier, T* value_ptr) {
		auto endpoint = findEndpoint(identifier);
		if (!endpoint)
			return false;

		return read<T>(endpoint->id, value_ptr);
	}

	template<typename T>
	bool write(uint16_t endpoint, T value) {

		if (!loaded || !connected)
			return false;

		std::vector<uint8_t> payload(sizeof(T), 0);
		memcpy(&payload[0], &value, sizeof(T));

		std::lock_guard<std::mutex> lock(transferMutex);
		if (sendWriteRequest(endpoint, sizeof(T), payload, jsonCRC) == -1) {
			LOG_WARN("Timeout: Failed to write endpoint {} value {}", endpoint, value);
			LOG_WARN("Written data was: Endpoint: {}, type {}, payload=value, crc=0x{:04X}", endpoint, typeid(T).name(), jsonCRC);
			return false;
		}

		return true;
	}

	template<typename T>
	bool write(const std::string& identifier, T value) {
		auto endpoint = findEndpoint(identifier);
		if (endpoint) {
			return write<T>(endpoint->id, value);
		}
		return false;
	}

	void executeFunction(const std::string& identifier) {
		auto endpoint = findEndpoint(identifier);
		if (endpoint) {
			std::lock_guard<std::mutex> lock(transferMutex);
			sendWriteRequest(endpoint->id, 1, { 0 }, jsonCRC);
		}
	}

	void exportEndpoints() {
		std::string file;

		uint8_t fw_version_major;
		uint8_t fw_version_minor;
		uint8_t fw_version_revision;
		uint8_t fw_version_unreleased;
		read<uint8_t>("fw_version_major", &fw_version_major);
		read<uint8_t>("fw_version_minor", &fw_version_minor);
		read<uint8_t>("fw_version_revision", &fw_version_revision);
		read<uint8_t>("fw_version_unreleased", &fw_version_unreleased);

		file += "\n// This file was auto-generated with https://github.com/HerrNamenlos123/ODriveGui\n";
		file += "// It defines the endpoints for use with the ODriveNativeLib Arduino library\n";
		file += "// FW version: " + 
			std::to_string(fw_version_major) + "." + 
			std::to_string(fw_version_minor) + "." +
			std::to_string(fw_version_revision) + ":" +
			std::to_string(fw_version_unreleased) + " with JSON CRC " + fmt::format("0x{:04X}", jsonCRC) + "\n";

		file += "//\n";
		file += "// Example:\n";
		file += "//\n";
		file += "//  odrv0.sendReadRequest<ENDPOINT_VBUS_VOLTAGE>();\n";
		file += "//\n";
		file += "// or\n";
		file += "//\n";
		file += "//  void OnDataCallback(uint16_t endpointID, uint8_t* data, size_t dataSize) {\n";
		file += "//  \n";
		file += "//      switch (endpointID) {\n";
		file += "//          ENDPOINT_CASE(vbus_voltage, ENDPOINT_VBUS_VOLTAGE); break;\n";
		file += "//          ENDPOINT_DEFAULT_CASE();\n";
		file += "//      }\n";
		file += "//  }\n";
		file += "//\n";
		file += "// ENDPOINT_CASE and ENDPOINT_DEFAULT_CASE should be defined in <ODrive.h> from the ODriveNativeLib\n";
		file += "//\n\n";

		file += "#ifndef __ENDPOINTS_H\n#define __ENDPOINTS_H\n\n";

		file += "#define JSON_CRC " + fmt::format("0x{:04X}", jsonCRC) + "\n\n";
		
		for (auto& endpoint : cachedEndpoints) {
			std::string identifier = replace(toUpper(endpoint.identifier), '.', '_');
			std::string type = endpoint.type;
			if (type.find("int") != std::string::npos) type += "_t";
			if (type == "function") type = "bool";
			file += "#define ENDPOINT_TYPE_" + identifier + " " + type + "\n";
			file += "#define ENDPOINT_ID_" + identifier + " " + std::to_string(endpoint.id) + "\n";
			file += "#define ENDPOINT_" + identifier + " JSON_CRC, ENDPOINT_ID_" + 
				identifier + ", ENDPOINT_TYPE_" + identifier + "\n\n";
		}

		file += "#endif // __ENDPOINTS_H\n";

		Battery::SaveFileWithDialog("h", file, Battery::GetMainWindow());
	}

	void updateErrors() {
		read<int32_t>("axis0.error", &axis0Error);
		read<int32_t>("axis0.motor.error", &motor0Error);
		read<int32_t>("axis0.encoder.error", &encoder0Error);
		read<int32_t>("axis0.controller.error", &controller0Error);
		read<int32_t>("axis1.error", &axis1Error);
		read<int32_t>("axis1.motor.error", &motor1Error);
		read<int32_t>("axis1.encoder.error", &encoder1Error);
		read<int32_t>("axis1.controller.error", &controller1Error);

		error = axis0Error || motor0Error || encoder0Error || controller0Error || 
			    axis1Error || motor1Error || encoder1Error || controller1Error;
	}

	uint64_t getSerialNumber() {
		read<uint64_t>("serial_number", &serialNumber);
		return serialNumber;
	}

	void load(int odriveID) {

		std::lock_guard<std::mutex> lock(transferMutex);
		connected = true;
		json = getJSON();
		jsonCRC = CRC16_JSON((uint8_t*)&json[0], json.length());
		setODriveID(odriveID);

		if (!connected)
			return;

		loaded = true;
		LOG_DEBUG("ODrive JSON CRC is 0x{:04X}", jsonCRC);
	}

	void setODriveID(int odriveID) {
		// Reload the cache
		generateEndpoints(odriveID);
	}

	operator bool() {
		return (bool)(connected && device && loaded);
	}

private:
	void disconnect() {
		connected = false;
		device->close();
	}

	void generateEndpoints(int odriveID) {

		endpoints.clear();
		cachedEndpoints.clear();

		try {

			for (auto& subnode : njson::parse(json)) {
				if (subnode["type"] == "json") {
					// Ignore the json endpoint (endpoint 0)
					continue;
				}

				endpoints.push_back(makeNode(subnode, "", odriveID));
			}
		}
		catch (...) {
			LOG_ERROR("Error while parsing json definition!");
			disconnect();
			endpoints.clear();
			cachedEndpoints.clear();
		}
	}

	Endpoint makeNode(njson node, const std::string& parentPath, int odriveID) {

		Endpoint ep;
		ep->name = node["name"];
		ep->identifier = ((parentPath.size() > 0) ? (parentPath + ".") : ("")) + ep->name;
		ep->fullPath = "odrv" + std::to_string(odriveID) + "." + ep->identifier;
		ep->type = node["type"];
		ep->odriveID = odriveID;

		if (ep->type == "object") {			// Object with children
			for (auto& subnode : node["members"]) {
				ep.children.push_back(makeNode(subnode, ep->identifier, odriveID));
			}
		}
		else if (ep->type == "function") {	// Function
			ep->id = node["id"];

			for (auto& input : node["inputs"]) {
				ep.inputs.push_back(makeNode(input, ep->identifier, odriveID));
			}
			for (auto& output : node["outputs"]) {
				ep.outputs.push_back(makeNode(output, ep->identifier, odriveID));
			}
			cachedEndpoints.push_back(ep.basic);
		}
		else {						// All other numeric types
			ep->id = node["id"];
			ep->readonly = (node["access"] == "r");
			cachedEndpoints.push_back(ep.basic);
		}

		return ep;
	}

	BasicEndpoint* findEndpoint(const std::string& identifier) {

		if (!loaded)
			return nullptr;

		for (auto& ep : cachedEndpoints) {
			if (ep.identifier == identifier) {
				return &ep;
			}
		}

		LOG_ERROR("Endpoint '{}' was not found in the cache", identifier);
		return nullptr;
	}

	uint16_t sendReadRequest(uint16_t endpointID, uint16_t expectedResponseSize, const buffer_t& payload, uint16_t jsonCRC) {
		return sendRequest((1 << 15) | endpointID, expectedResponseSize, payload, jsonCRC);
	}

	uint16_t sendWriteRequest(uint16_t endpointID, uint16_t expectedResponseSize, const buffer_t& payload, uint16_t jsonCRC) {
		return sendRequest(endpointID, expectedResponseSize, payload, jsonCRC);
	}

	uint16_t sendRequest(uint16_t endpointID, uint16_t expectedResponseSize, const buffer_t& payload, uint16_t jsonCRC) {
		sequenceNumber = (sequenceNumber + 1) % 4096;
		if (sendRequest(sequenceNumber, endpointID, expectedResponseSize, payload, jsonCRC))
			return sequenceNumber;
		else
			return -1;
	}

	bool sendRequest(uint16_t sequenceNumber, uint16_t endpointID, uint16_t expectedResponseSize, const buffer_t& payload, uint16_t jsonCRC) {
		buffer_t buffer;
		buffer.reserve(payload.size() + 8);

		buffer.push_back((uint8_t)(sequenceNumber));
		buffer.push_back((uint8_t)(sequenceNumber >> 8));

		buffer.push_back((uint8_t)(endpointID));
		buffer.push_back((uint8_t)(endpointID >> 8));

		buffer.push_back((uint8_t)(expectedResponseSize));
		buffer.push_back((uint8_t)(expectedResponseSize >> 8));

		for (uint8_t b : payload) {
			buffer.push_back(b);
		}

		buffer.push_back((uint8_t)(jsonCRC));
		buffer.push_back((uint8_t)(jsonCRC >> 8));

		return write(&buffer[0], buffer.size());
	}

	bool write(uint8_t* data, size_t length) {
		for (int i = 0; i < 5; i++) {
			if (device->bulkWrite(data, length, ODRIVE_USB_WRITE_ENDPOINT) != -1) {
				return true;
			}
		}
		disconnect();
		return false;
	}

	std::vector<uint8_t> read(size_t expectedLength) {
		auto data = device->bulkRead(expectedLength + 2, ODRIVE_USB_READ_ENDPOINT);
		if (data.size() == 0) {
			disconnect();
		}
		return data;
	}

	std::pair<uint16_t, buffer_t> getResponse(size_t expectedLength) {
		buffer_t response = read(expectedLength);

		if (response.size() < 2) {
			return std::make_pair(0, buffer_t());
		}

		uint16_t sequence = response[0] | response[1] << 8;

		buffer_t buffer;
		for (size_t i = 2; i < response.size(); i++) {
			buffer.push_back(response[i]);
		}
		return std::make_pair(sequence, buffer);
	}

	std::string getJSON() {
		std::string json;

		uint32_t offset = 0;
		while (true) {
			buffer_t buffer(sizeof(offset), 0);
			memcpy(&buffer[0], &offset, sizeof(offset));

			sendReadRequest(0, 32, buffer, 1);
			auto response = getResponse(32);
			offset += (uint32_t)response.second.size();

			if (response.second.size() == 0) {
				break;
			}

			json += std::string((char*)&response.second[0], response.second.size());
		}

		return json;
	}

	libusbcpp::device device;
	inline static uint16_t sequenceNumber = 0;
	std::mutex transferMutex;
};
