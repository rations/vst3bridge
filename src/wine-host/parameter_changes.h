/*
 * Copyright (C) 2026
 * VST3 Bridge - Wine VST3 Host Bridge
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

/**
 * @file parameter_changes.h
 * @brief VST3 IParameterChanges implementation for Wine host
 * 
 * This file provides full implementations of the VST3 parameter
 * change interfaces required by ProcessData during audio processing.
 * These interfaces allow plugins to receive parameter automation
 * from the DAW.
 */

#ifndef VST3BRIDGE_PARAMETER_CHANGES_H
#define VST3BRIDGE_PARAMETER_CHANGES_H

#include <vector>
#include <map>
#include <algorithm>
#include "pluginterfaces/vst/ivstparameterchanges.h"

namespace vst3bridge {

/**
 * @brief Implementation of VST3 IParamValueQueue
 * 
 * Represents a queue of parameter value changes for a single parameter
 * during one processing block. Multiple value changes can occur within
 * a single buffer (e.g., for smooth automation curves).
 */
class ParamValueQueue : public Steinberg::Vst::IParamValueQueue {
public:
    /**
     * @brief Single parameter value change point
     */
    struct Point {
        int32_t sampleOffset;  ///< Sample offset within buffer
        Steinberg::Vst::ParamValue value;  ///< Parameter value (0.0 to 1.0)
    };

    ParamValueQueue();
    virtual ~ParamValueQueue();

    // FUnknown interface
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;

    // IParamValueQueue interface
    Steinberg::Vst::ParamID PLUGIN_API getParameterId() override;
    int32_t PLUGIN_API getPointCount() override;
    Steinberg::tresult PLUGIN_API getPoint(int32_t index, 
                                           int32_t& sampleOffset, 
                                           Steinberg::Vst::ParamValue& value) override;
    Steinberg::tresult PLUGIN_API addPoint(int32_t sampleOffset, 
                                          Steinberg::Vst::ParamValue value, 
                                          int32_t& index) override;

    /**
     * @brief Set the parameter ID for this queue
     * @param id Parameter ID
     */
    void setParameterId(Steinberg::Vst::ParamID id);

    /**
     * @brief Clear all points from the queue
     */
    void clear();

    /**
     * @brief Add a point (internal version without index output)
     * @param sampleOffset Sample offset in buffer
     * @param value Parameter value
     * @return True on success
     */
    bool addPoint(int32_t sampleOffset, Steinberg::Vst::ParamValue value);

private:
    std::vector<Point> points_;  ///< Sorted vector of parameter changes
    Steinberg::Vst::ParamID parameterId_;  ///< Parameter ID for this queue
    Steinberg::int32 refCount_;
};

/**
 * @brief Implementation of VST3 IParameterChanges
 * 
 * Manages a collection of parameter value queues, one per parameter
 * that has changes during the current processing block. This is the
 * main interface plugins use to receive parameter automation.
 */
class ParameterChanges : public Steinberg::Vst::IParameterChanges {
public:
    ParameterChanges();
    virtual ~ParameterChanges();

    // FUnknown interface
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;

    // IParameterChanges interface
    int32_t PLUGIN_API getParameterCount() override;
    Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(int32_t index) override;
    Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
        const Steinberg::Vst::ParamID& id, 
        int32_t& index) override;

    /**
     * @brief Clear all parameter changes
     */
    void clear();

    /**
     * @brief Add a parameter value change
     * @param id Parameter ID
     * @param sampleOffset Sample offset in buffer
     * @param value Parameter value (0.0 to 1.0)
     * @return True on success
     */
    bool addParameterChange(Steinberg::Vst::ParamID id, 
                           int32_t sampleOffset, 
                           Steinberg::Vst::ParamValue value);

    /**
     * @brief Reserve space for parameter changes
     * @param count Expected number of parameters with changes
     */
    void reserve(int32_t count);

private:
    std::vector<ParamValueQueue*> queues_;  ///< Vector of parameter queues
    std::map<Steinberg::Vst::ParamID, ParamValueQueue*> queueMap_;  ///< Map for fast lookup
    Steinberg::int32 refCount_;
};

/**
 * @brief Builder for constructing ParameterChanges from IPC data
 * 
 * This helper class makes it easier to build parameter change
 * structures from IPC messages received from the native side.
 */
class ParameterChangesBuilder {
public:
    ParameterChangesBuilder();
    ~ParameterChangesBuilder();

    /**
     * @brief Add a parameter change
     * @param paramId Parameter ID
     * @param sampleOffset Sample offset in buffer
     * @param value Parameter value
     */
    void addChange(Steinberg::Vst::ParamID paramId, 
                   int32_t sampleOffset, 
                   Steinberg::Vst::ParamValue value);

    /**
     * @brief Build the ParameterChanges object
     * @return Pointer to built ParameterChanges (caller owns)
     */
    ParameterChanges* build();

    /**
     * @brief Clear all accumulated changes
     */
    void clear();

    /**
     * @brief Get count of parameters with changes
     * @return Number of unique parameters
     */
    int32_t getParameterCount() const;

private:
    std::map<Steinberg::Vst::ParamID, std::vector<std::pair<int32_t, Steinberg::Vst::ParamValue>>> changes_;
};

} // namespace vst3bridge

#endif // VST3BRIDGE_PARAMETER_CHANGES_H