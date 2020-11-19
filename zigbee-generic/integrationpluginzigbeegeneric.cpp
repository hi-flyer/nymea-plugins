﻿/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
*
* Copyright 2013 - 2020, nymea GmbH
* Contact: contact@nymea.io

* This file is part of nymea.
* This project including source code and documentation is protected by
* copyright law, and remains the property of nymea GmbH. All rights, including
* reproduction, publication, editing and translation, are reserved. The use of
* this project is subject to the terms of a license agreement to be concluded
* with nymea GmbH in accordance with the terms of use of nymea GmbH, available
* under https://nymea.io/license
*
* GNU Lesser General Public License Usage
* Alternatively, this project may be redistributed and/or modified under the
* terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; version 3. This project is distributed in the hope that
* it will be useful, but WITHOUT ANY WARRANTY; without even the implied
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this project. If not, see <https://www.gnu.org/licenses/>.
*
* For any further details and any questions please contact us under
* contact@nymea.io or see our FAQ/Licensing Information on
* https://nymea.io/license/faq
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "integrationpluginzigbeegeneric.h"
#include "plugininfo.h"
#include "hardware/zigbee/zigbeehardwareresource.h"

#include "zcl/hvac/zigbeeclusterthermostat.h"

#include <QDebug>

IntegrationPluginZigbeeGeneric::IntegrationPluginZigbeeGeneric()
{
    m_ieeeAddressParamTypeIds[thermostatThingClassId] = thermostatThingIeeeAddressParamTypeId;
    m_ieeeAddressParamTypeIds[powerSocketThingClassId] = powerSocketThingIeeeAddressParamTypeId;

    m_networkUuidParamTypeIds[thermostatThingClassId] = thermostatThingNetworkUuidParamTypeId;
    m_networkUuidParamTypeIds[powerSocketThingClassId] = powerSocketThingNetworkUuidParamTypeId;

    m_endpointIdParamTypeIds[thermostatThingClassId] = thermostatThingEndpointIdParamTypeId;
    m_endpointIdParamTypeIds[powerSocketThingClassId] = powerSocketThingEndpointIdParamTypeId;

    m_manufacturerIdParamTypeIds[thermostatThingClassId] = thermostatThingManufacturerParamTypeId;
    m_manufacturerIdParamTypeIds[powerSocketThingClassId] = powerSocketThingManufacturerParamTypeId;

    m_modelIdParamTypeIds[thermostatThingClassId] = thermostatThingModelParamTypeId;
    m_modelIdParamTypeIds[powerSocketThingClassId] = powerSocketThingModelParamTypeId;

    m_connectedStateTypeIds[thermostatThingClassId] = thermostatConnectedStateTypeId;
    m_connectedStateTypeIds[powerSocketThingClassId] = powerSocketConnectedStateTypeId;

    m_signalStrengthStateTypeIds[thermostatThingClassId] = thermostatSignalStrengthStateTypeId;
    m_signalStrengthStateTypeIds[powerSocketThingClassId] = powerSocketSignalStrengthStateTypeId;

    m_versionStateTypeIds[thermostatThingClassId] = thermostatVersionStateTypeId;
    m_versionStateTypeIds[powerSocketThingClassId] = powerSocketVersionStateTypeId;
}

QString IntegrationPluginZigbeeGeneric::name() const
{
    return "Generic";
}

bool IntegrationPluginZigbeeGeneric::handleNode(ZigbeeNode *node, const QUuid &networkUuid)
{
    bool handled = false;
    foreach (ZigbeeNodeEndpoint *endpoint, node->endpoints()) {
        qCDebug(dcZigbeeGeneric()) << "Checking node endpoint:" << endpoint->endpointId() << endpoint->deviceId();
        if (endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileHomeAutomation &&
                endpoint->deviceId() == Zigbee::HomeAutomationDeviceThermostat) {
            qCDebug(dcZigbeeGeneric()) << "Handeling thermostat endpoint for" << node << endpoint;
            createThing(thermostatThingClassId, networkUuid, node, endpoint);
            handled = true;
        }

        if ((endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileLightLink &&
             endpoint->deviceId() == Zigbee::LightLinkDevice::LightLinkDeviceOnOffPlugin) ||
                (endpoint->profile() == Zigbee::ZigbeeProfile::ZigbeeProfileHomeAutomation &&
                 endpoint->deviceId() == Zigbee::HomeAutomationDeviceOnOffPlugin)) {

            qCDebug(dcZigbeeGeneric()) << "Handeling power socket endpoint for" << node << endpoint;
            createThing(powerSocketThingClassId, networkUuid, node, endpoint);
            handled = true;
        }
    }

    return handled;
}

void IntegrationPluginZigbeeGeneric::handleRemoveNode(ZigbeeNode *node, const QUuid &networkUuid)
{
    Q_UNUSED(networkUuid)
    Thing *thing = m_thingNodes.key(node);
    if (thing) {
        qCDebug(dcZigbeeGeneric()) << node << "for" << thing << "has left the network.";
        emit autoThingDisappeared(thing->id());

        // Removing it from our map to prevent a loop that would ask the zigbee network to remove this node (see thingRemoved())
        m_thingNodes.remove(thing);
    }
}

void IntegrationPluginZigbeeGeneric::init()
{
    hardwareManager()->zigbeeResource()->registerHandler(this, ZigbeeHardwareResource::HandlerTypeCatchAll);
}

void IntegrationPluginZigbeeGeneric::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();
    QUuid networkUuid = thing->paramValue(m_networkUuidParamTypeIds.value(thing->thingClassId())).toUuid();
    qCDebug(dcZigbeeGeneric()) << "Nework uuid:" << networkUuid;
    ZigbeeAddress zigbeeAddress = ZigbeeAddress(thing->paramValue(m_ieeeAddressParamTypeIds.value(thing->thingClassId())).toString());
    ZigbeeNode *node = m_thingNodes.value(thing);
    if (!node) {
        node = hardwareManager()->zigbeeResource()->claimNode(this, networkUuid, zigbeeAddress);
    }

    if (!node) {
        qCWarning(dcZigbeeGeneric()) << "Zigbee node for" << info->thing()->name() << "not found.´";
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }
    m_thingNodes.insert(thing, node);

    ZigbeeNodeEndpoint *endpoint = findEndpoint(thing);
    if (!endpoint) {
        qCWarning(dcZigbeeGeneric()) << "Could not find endpoint for" << thing;
        info->finish(Thing::ThingErrorSetupFailed);
        return;
    }

    // Update connected state
    thing->setStateValue(m_connectedStateTypeIds.value(thing->thingClassId()), node->reachable());
    connect(node, &ZigbeeNode::reachableChanged, thing, [thing, this](bool reachable){
        thing->setStateValue(m_connectedStateTypeIds.value(thing->thingClassId()), reachable);
    });

    // Update signal strength
    thing->setStateValue(m_signalStrengthStateTypeIds.value(thing->thingClassId()), qRound(node->lqi() * 100.0 / 255.0));
    connect(node, &ZigbeeNode::lqiChanged, thing, [this, thing](quint8 lqi){
        uint signalStrength = qRound(lqi * 100.0 / 255.0);
        qCDebug(dcZigbeeGeneric()) << thing << "signal strength changed" << signalStrength << "%";
        thing->setStateValue(m_signalStrengthStateTypeIds.value(thing->thingClassId()), signalStrength);
    });

    // Set the version
    thing->setStateValue(m_versionStateTypeIds.value(thing->thingClassId()), endpoint->softwareBuildId());

    // Type specific setup
    if (thing->thingClassId() == thermostatThingClassId) {
        ZigbeeClusterThermostat *thermostatCluster = endpoint->inputCluster<ZigbeeClusterThermostat>(ZigbeeClusterLibrary::ClusterIdThermostat);
        if (!thermostatCluster) {
            qCWarning(dcZigbeeGeneric()) << "Failed to read thermostat cluster";
            return;
        }
//        thermostatCluster->attribute(ZigbeeClusterLibrary::ClusterIdThermostat);

        // We need to read them from the lamp
        ZigbeeClusterReply *reply = thermostatCluster->readAttributes({ZigbeeClusterThermostat::AttributeLocalTemperature, ZigbeeClusterThermostat::AttributeOccupiedHeatingSetpoint});
        connect(reply, &ZigbeeClusterReply::finished, thing, [=](){
            if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                qCWarning(dcZigbeeGeneric()) << "Reading loacal temperature attribute finished with error" << reply->error();
                return;
            }

            QList<ZigbeeClusterLibrary::ReadAttributeStatusRecord> attributeStatusRecords = ZigbeeClusterLibrary::parseAttributeStatusRecords(reply->responseFrame().payload);

            foreach (const ZigbeeClusterLibrary::ReadAttributeStatusRecord &record, attributeStatusRecords) {
                if (record.attributeId == ZigbeeClusterThermostat::AttributeLocalTemperature) {
                    bool valueOk = false;
                    quint16 localTemperature = record.dataType.toUInt16(&valueOk);
                    if (!valueOk) {
                        qCWarning(dcZigbeeGeneric()) << "Failed to read local temperature" << attributeStatusRecords;
                        return;
                    }
                    thing->setStateValue(thermostatTemperatureStateTypeId, localTemperature * 0.01);
                }

                if (record.attributeId == ZigbeeClusterThermostat::AttributeOccupiedHeatingSetpoint) {
                    bool valueOk = false;
                    quint16 targetTemperature = record.dataType.toUInt16(&valueOk);
                    if (!valueOk) {
                        qCWarning(dcZigbeeGeneric()) << "Failed to read local temperature" << attributeStatusRecords;
                        return;
                    }
                    thing->setStateValue(thermostatTargetTemperatureStateTypeId, targetTemperature * 0.01);
                }
            }



        });
    }

    if (thing->thingClassId() == powerSocketThingClassId) {
        ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
        if (onOffCluster) {
            if (onOffCluster->hasAttribute(ZigbeeClusterOnOff::AttributeOnOff)) {
                thing->setStateValue(powerSocketPowerStateTypeId, onOffCluster->power());
            }

            connect(onOffCluster, &ZigbeeClusterOnOff::powerChanged, thing, [thing](bool power){
                qCDebug(dcZigbeeGeneric()) << thing << "power changed" << power;
                thing->setStateValue(powerSocketPowerStateTypeId, power);
            });

            connect(node, &ZigbeeNode::reachableChanged, thing, [=](bool reachable){
               if (reachable) {
                   ZigbeeClusterReply *reply = onOffCluster->readAttributes({ZigbeeClusterOnOff::AttributeOnOff});
                   connect(reply, &ZigbeeClusterReply::finished, thing, [=](){
                       if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                           qCWarning(dcZigbeeGeneric()) << "Reading attribute from" << thing << "finished with error" << reply->error();
                       }
                       // Note: the state will be updated using the power changed signal from the cluster
                   });
               }
            });
        } else {
            qCWarning(dcZigbeeGeneric()) << "Could not find the OnOff input cluster on" << thing << endpoint;
        }
    }

    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginZigbeeGeneric::executeAction(ThingActionInfo *info)
{
    if (!hardwareManager()->zigbeeResource()->available()) {
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    // Get the node
    Thing *thing = info->thing();
    ZigbeeNode *node = m_thingNodes.value(thing);
    if (!node->reachable()) {
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    // Get the endpoint
    ZigbeeNodeEndpoint *endpoint = findEndpoint(thing);
    if (!endpoint) {
        info->finish(Thing::ThingErrorHardwareNotAvailable);
        return;
    }

    if (thing->thingClassId() == powerSocketThingClassId) {
        if (info->action().actionTypeId() == powerSocketAlertActionTypeId) {
            ZigbeeClusterIdentify *identifyCluster = endpoint->inputCluster<ZigbeeClusterIdentify>(ZigbeeClusterLibrary::ClusterIdIdentify);
            if (!identifyCluster) {
                qCWarning(dcZigbeeGeneric()) << "Could not find identify cluster for" << thing << "in" << m_thingNodes.value(thing);
                info->finish(Thing::ThingErrorHardwareFailure);
                return;
            }

            // Send the command trough the network
            ZigbeeClusterReply *reply = identifyCluster->identify(2);
            connect(reply, &ZigbeeClusterReply::finished, this, [reply, info](){
                // Note: reply will be deleted automatically
                if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                    info->finish(Thing::ThingErrorHardwareFailure);
                } else {
                    info->finish(Thing::ThingErrorNoError);
                }
            });
            return;
        }

        if (info->action().actionTypeId() == powerSocketPowerActionTypeId) {
            ZigbeeClusterOnOff *onOffCluster = endpoint->inputCluster<ZigbeeClusterOnOff>(ZigbeeClusterLibrary::ClusterIdOnOff);
            if (!onOffCluster) {
                qCWarning(dcZigbeeGeneric()) << "Could not find on/off cluster for" << thing << "in" << endpoint;
                info->finish(Thing::ThingErrorHardwareFailure);
                return;
            }

            // Send the command trough the network
            bool power = info->action().param(powerSocketPowerActionPowerParamTypeId).value().toBool();
            qCDebug(dcZigbeeGeneric()) << "Set power for" << thing << "to" << power;
            ZigbeeClusterReply *reply = (power ? onOffCluster->commandOn() : onOffCluster->commandOff());
            connect(reply, &ZigbeeClusterReply::finished, info, [=](){
                // Note: reply will be deleted automatically
                if (reply->error() != ZigbeeClusterReply::ErrorNoError) {
                    qCWarning(dcZigbeeGeneric()) << "Failed to set power on" << thing << reply->error();
                    info->finish(Thing::ThingErrorHardwareFailure);
                } else {
                    info->finish(Thing::ThingErrorNoError);
                    qCDebug(dcZigbeeGeneric()) << "Set power finished successfully for" << thing;
                    thing->setStateValue(powerSocketPowerStateTypeId, power);
                }
            });
            return;
        }
    }

    info->finish(Thing::ThingErrorUnsupportedFeature);
}

void IntegrationPluginZigbeeGeneric::thingRemoved(Thing *thing)
{
    ZigbeeNode *node = m_thingNodes.take(thing);
    if (node) {
        QUuid networkUuid = thing->paramValue(m_networkUuidParamTypeIds.value(thing->thingClassId())).toUuid();
        hardwareManager()->zigbeeResource()->removeNodeFromNetwork(networkUuid, node);
    }
}

ZigbeeNodeEndpoint *IntegrationPluginZigbeeGeneric::findEndpoint(Thing *thing)
{
    ZigbeeNode *node = m_thingNodes.value(thing);
    if (!node) {
        qCWarning(dcZigbeeGeneric()) << "Could not find the node for" << thing;
        return nullptr;
    }

    quint8 endpointId = thing->paramValue(m_endpointIdParamTypeIds.value(thing->thingClassId())).toUInt();
    return node->getEndpoint(endpointId);
}

void IntegrationPluginZigbeeGeneric::createThing(const ThingClassId &thingClassId, const QUuid &networkUuid, ZigbeeNode *node, ZigbeeNodeEndpoint *endpoint)
{
    ThingDescriptor descriptor(thingClassId);
    QString deviceClassName = supportedThings().findById(thingClassId).displayName();
    descriptor.setTitle(QString("%1 (%2 - %3)").arg(deviceClassName).arg(endpoint->manufacturerName()).arg(endpoint->modelIdentifier()));

    ParamList params;
    params.append(Param(m_networkUuidParamTypeIds[thingClassId], networkUuid.toString()));
    params.append(Param(m_ieeeAddressParamTypeIds[thingClassId], node->extendedAddress().toString()));
    params.append(Param(m_endpointIdParamTypeIds[thingClassId], endpoint->endpointId()));
    params.append(Param(m_modelIdParamTypeIds[thingClassId], endpoint->modelIdentifier()));
    params.append(Param(m_manufacturerIdParamTypeIds[thingClassId], endpoint->manufacturerName()));
    descriptor.setParams(params);
    emit autoThingsAppeared({descriptor});
}