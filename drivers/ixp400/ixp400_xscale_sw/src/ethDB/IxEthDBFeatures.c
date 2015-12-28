/**
 * @file IxEthDBFeatures.c
 *
 * @brief Implementation of the EthDB feature control API
 * 
 * @par
 * IXP400 SW Release version 2.4
 * 
 * -- Copyright Notice --
 * 
 * @par
 * Copyright (c) 2001-2007, Intel Corporation.
 * All rights reserved.
 * 
 * @par
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * 
 * @par
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * 
 * @par
 * -- End of Copyright Notice --
 */

#include "IxNpeDl.h"
#include "IxEthDBQoS.h"
#include "IxEthDB_p.h"

/** 
 * @brief Port definitions structure, indexed on the port ID
 * @warning Ports 0 and 1 are used by the Ethernet access component therefore
 * it is essential to be left untouched. Port 2 can be Ethernet Port or
 * WAN port. Port 3 here (WAN) is given as an example port. The NPE firmware 
 * also assumes the NPE B to be the port 0, NPE C to be the port 1, and
 * NPE A to be port 2.
 *
 * @note that only 32 ports (0..31) are supported by EthDB
 */
IxEthDBPortDefinition ixEthDBPortDefinitions[IX_ETH_DB_NUMBER_OF_PORTS] = 
{
    /*    id       type       */
    {   /* 0 */    IX_ETH_NPE },    /* Ethernet NPE B */
    {   /* 1 */    IX_ETH_NPE },    /* Ethernet NPE C */
    {   /* 2 */    IX_ETH_NPE }     /* Ethernet NPE A or WAN Port */
};

/**
 * @brief scans the capabilities of the loaded NPE images
 *
 * This function MUST be called by the ixEthDBInit() function.
 * No EthDB features (including learning and filtering) are enabled
 * before this function is called.
 *
 * @return none
 *
 * @internal
 */
IX_ETH_DB_PUBLIC
void ixEthDBFeatureCapabilityScan(void)
{
    UINT8 functionalityId, npeAFunctionalityId;
    IxEthDBPortId portIndex;
    PortInfo *portInfo;
    IxEthDBPriorityTable defaultPriorityTable;
    IX_STATUS result;
    UINT32 queueIndex;
    UINT32 queueStructureIndex;
    UINT32 trafficClassDefinitionIndex, totalTrafficClass;

    totalTrafficClass = sizeof (ixEthDBTrafficClassDefinitions) / sizeof (ixEthDBTrafficClassDefinitions[0]);

    /* ensure there's at least 2 traffic class records in the definition table, otherwise we have no default cases, hence no queues */
    IX_ENSURE(totalTrafficClass >= 2, 
	"DB: no traffic class definitions found, check IxEthDBQoS.h");

    /* read version of NPE A - required to set the AQM queues for B and C */
    npeAFunctionalityId = 0;

    if(IX_FAIL == ixNpeDlLoadedImageFunctionalityGet(IX_NPEDL_NPEID_NPEA, &npeAFunctionalityId))
    {
        /* IX_FAIL is returned when there is no image loaded in NPEA.  Then we can use all 8 queues */
        trafficClassDefinitionIndex = 1; /* the second record is the default if no image loaded */
    } 
    else 
    {
        /* find the traffic class definition index compatible with the current NPE A functionality ID */
        for (trafficClassDefinitionIndex = 0 ; 
             trafficClassDefinitionIndex < totalTrafficClass ;
             trafficClassDefinitionIndex++)
        {
            if (ixEthDBTrafficClassDefinitions[trafficClassDefinitionIndex][IX_ETH_DB_NPE_A_FUNCTIONALITY_ID_INDEX] == npeAFunctionalityId)
            {
                /* found it */
                break;
            }
        }

        /* select the default case if we went over the array boundary */
        if (trafficClassDefinitionIndex == totalTrafficClass)
        {
            trafficClassDefinitionIndex = 0; /* the first record is the default case */
        }
    }

    /* To decide port definition for NPE A - IX_ETH_NPE or IX_ETH_GENERIC 
       IX_ETH_NPE will be set for NPE A when the functionality id is ranged from 0x80 to 0x8F
       and ethernet + hss co-exists images range from 0x90 to 0x9F. For the rest of functionality 
       Ids, the port type will be set to IX_ETH_GENERIC. */
    if ((npeAFunctionalityId & 0xF0) != 0x80 && (npeAFunctionalityId & 0xF0) != 0x90)
    {
        /* NPEA is not Ethernet capable. Override default port definition */
	ixEthDBPortDefinitions[IX_NPEA_PORT].type = IX_ETH_GENERIC;
    }

    /* select queue assignment structure based on the traffic class configuration index */
    queueStructureIndex = ixEthDBTrafficClassDefinitions[trafficClassDefinitionIndex][IX_ETH_DB_QUEUE_ASSIGNMENT_INDEX];

    for (portIndex = 0 ; portIndex < IX_ETH_DB_NUMBER_OF_PORTS ; portIndex++)
    {
        IxNpeMhMessage msg;

        portInfo = &ixEthDBPortInfo[portIndex];

        /* check and bypass if NPE A, B or C is fused out */
        if (ixEthDBSingleEthNpeCheck(portIndex) != IX_ETH_DB_SUCCESS) continue;
        
        /* all ports are capable of LEARNING by default */
        portInfo->featureCapability = IX_ETH_DB_LEARNING;
        portInfo->featureStatus     = IX_ETH_DB_LEARNING;

        if (ixEthDBPortDefinitions[portIndex].type == IX_ETH_NPE)
        {

            if (IX_SUCCESS != ixNpeDlLoadedImageFunctionalityGet(IX_ETHNPE_PHYSICAL_ID_TO_NODE(portIndex), &functionalityId))
            {
                WARNING_LOG("DB: (FeatureScan) NpeDl did not provide the image ID for NPE port %d\n", portIndex);
            }
            else
            {
                /* initialize and empty NPE response mutex */
                ixOsalMutexInit(&portInfo->npeAckLock);
		ixOsalMutexLock(&portInfo->npeAckLock, IX_OSAL_WAIT_FOREVER);
                /* check NPE response to GetStatus */
                msg.data[0] = IX_ETHNPE_NPE_GETSTATUS << 24;
                msg.data[1] = 0;
                IX_ETHDB_SEND_NPE_MSG(IX_ETHNPE_PHYSICAL_ID_TO_NODE(portIndex), msg, result);
                if (result != IX_SUCCESS)
                {
                    WARNING_LOG("DB: (FeatureScan) warning, %d port could not send message to the NPE\n", portIndex);
                    continue;
                }

                if (functionalityId == 0x00
                    || functionalityId == 0x03
                    || functionalityId == 0x04
                    || functionalityId == 0x80)
                {
                    portInfo->featureCapability |= IX_ETH_DB_FILTERING;
                    portInfo->featureCapability |= IX_ETH_DB_FIREWALL;
                    portInfo->featureCapability |= IX_ETH_DB_SPANNING_TREE_PROTOCOL;
                }
                else if (functionalityId == 0x01
                         || functionalityId == 0x81
                         || functionalityId == 0x0B
                         || functionalityId == 0x8B
                         || functionalityId == 0x90)
                {
                    portInfo->featureCapability |= IX_ETH_DB_FILTERING;
                    portInfo->featureCapability |= IX_ETH_DB_FIREWALL;
                    portInfo->featureCapability |= IX_ETH_DB_SPANNING_TREE_PROTOCOL;
                    portInfo->featureCapability |= IX_ETH_DB_VLAN_QOS;
                }
                else if (functionalityId == 0x02
                         || functionalityId == 0x82
                         || functionalityId == 0x0D
                         || functionalityId == 0x8D
                         || functionalityId == 0x91)
                {
                    portInfo->featureCapability |= IX_ETH_DB_WIFI_HEADER_CONVERSION;
                    portInfo->featureCapability |= IX_ETH_DB_FIREWALL;
                    portInfo->featureCapability |= IX_ETH_DB_SPANNING_TREE_PROTOCOL;
                    portInfo->featureCapability |= IX_ETH_DB_VLAN_QOS;
                }
                else if (functionalityId == 0x0C
			 || functionalityId == 0x8C)
                {
                    portInfo->featureCapability |= IX_ETH_DB_WIFI_HEADER_CONVERSION;
                    portInfo->featureCapability |= IX_ETH_DB_SPANNING_TREE_PROTOCOL;
                    portInfo->featureCapability |= IX_ETH_DB_VLAN_QOS;
                }

                /* check if image supports mask based firewall */
                if (functionalityId == 0x0B
                    || functionalityId == 0x8B
                    || functionalityId == 0x0D
                    || functionalityId == 0x8D
                    || functionalityId == 0x90
                    || functionalityId == 0x91)
                {
                    /* this feature is always on and is based on the NPE */
                    portInfo->featureStatus |= IX_ETH_DB_ADDRESS_MASKING;
                    portInfo->featureCapability |= IX_ETH_DB_ADDRESS_MASKING;
                }

                /* reset AQM queues */
                ixOsalMemSet(portInfo->ixEthDBTrafficClassAQMAssignments, 0, sizeof (portInfo->ixEthDBTrafficClassAQMAssignments));

                /* only traffic class 0 is active at initialization time */
                portInfo->ixEthDBTrafficClassCount = 1;

                /* enable port, VLAN and Firewall feature bits to initialize QoS/VLAN/Firewall configuration */
                portInfo->featureStatus |= IX_ETH_DB_VLAN_QOS;
                portInfo->featureStatus |= IX_ETH_DB_FIREWALL;
                portInfo->enabled        = TRUE;

                /* set VLAN initial configuration (permissive) */
                if ((portInfo->featureCapability & IX_ETH_DB_VLAN_QOS) != 0) /* QoS-enabled image */
                {
                    /* QoS capable */
                    portInfo->ixEthDBTrafficClassAvailable = ixEthDBTrafficClassDefinitions[trafficClassDefinitionIndex][IX_ETH_DB_TRAFFIC_CLASS_COUNT_INDEX];

                    /* set AQM queues */
                    for (queueIndex = 0 ; queueIndex < IX_IEEE802_1Q_QOS_PRIORITY_COUNT ; queueIndex++)
                    {
                        portInfo->ixEthDBTrafficClassAQMAssignments[queueIndex] = ixEthDBQueueAssignments[queueStructureIndex][queueIndex];
                    }

                    /* set default PVID (0) and default traffic class 0 */
                    ixEthDBPortVlanTagSet(portIndex, 0);

                    /* enable reception of all frames */
                    ixEthDBAcceptableFrameTypeSet(portIndex, IX_ETH_DB_ACCEPT_ALL_FRAMES);

                    /* clear full VLAN membership */
                    ixEthDBPortVlanMembershipRangeRemove(portIndex, 0, IX_ETH_DB_802_1Q_MAX_VLAN_ID);

                    /* clear TTI table - no VLAN tagged frames will be transmitted */
                    ixEthDBEgressVlanRangeTaggingEnabledSet(portIndex, 0, 4094, FALSE);

                    /* set membership on 0, otherwise no Tx or Rx is working */
                    ixEthDBPortVlanMembershipAdd(portIndex, 0);
                }
                else /* QoS not available in this image */
                {
                    /* initialize traffic class availability (only class 0 is available) */
                    portInfo->ixEthDBTrafficClassAvailable = 1;

                    /* point all AQM queues to traffic class 0 */
                    for (queueIndex = 0 ; queueIndex < IX_IEEE802_1Q_QOS_PRIORITY_COUNT ; queueIndex++)
                    {
                        portInfo->ixEthDBTrafficClassAQMAssignments[queueIndex] = 
                            ixEthDBQueueAssignments[queueStructureIndex][0];
                    }
                }

                /* download priority mapping table and Rx queue configuration */
                ixOsalMemSet (defaultPriorityTable, 0, sizeof (defaultPriorityTable));
                ixEthDBPriorityMappingTableSet(portIndex, defaultPriorityTable);

                /* by default we turn on invalid source MAC address filtering */
                ixEthDBFirewallInvalidAddressFilterEnable(portIndex, TRUE);

                /* Notify VLAN tagging is disabled */
		if (ixEthDBIngressVlanTaggingEnabledSet(portIndex, IX_ETH_DB_DISABLE_VLAN)
		    != IX_SUCCESS)
		{
		  WARNING_LOG("DB: (FeatureScan) warning, %d port could not disable VLAN \n", portIndex);
		  continue;		  
		}

                /* disable port, VLAN, Firewall feature bits */
                portInfo->featureStatus &= ~IX_ETH_DB_VLAN_QOS;
                portInfo->featureStatus &= ~IX_ETH_DB_FIREWALL;
                portInfo->enabled        = FALSE;

                /* enable filtering by default if present */
                if ((portInfo->featureCapability & IX_ETH_DB_FILTERING) != 0)
                {
                    portInfo->featureStatus |= IX_ETH_DB_FILTERING;
                }
            }
        } 
    }
}

/**
 * @brief returns the capability of a port
 *
 * @param portID ID of the port
 * @param featureSet location to store the port capability in
 *
 * This function will save the capability set of the given port
 * into the given location. Capabilities are bit-ORed, each representing
 * a bit of the feature set.
 *
 * Note that this function is documented in the main component
 * public header file, IxEthDB.h.
 *
 * @return IX_ETH_DB_SUCCESS if the operation completed successfully
 * or IX_ETH_DB_INVALID_PORT if the given port is invalid
 */
IX_ETH_DB_PUBLIC 
IxEthDBStatus ixEthDBFeatureCapabilityGet(IxEthDBPortId portID, IxEthDBFeature *featureSet)
{
    IX_ETH_DB_CHECK_PORT_INITIALIZED(portID);

    IX_ETH_DB_CHECK_REFERENCE(featureSet);
    
    *featureSet = ixEthDBPortInfo[portID].featureCapability;
    
    return IX_ETH_DB_SUCCESS;
}

/**
 * @brief enables or disables a port capability
 *
 * @param portID ID of the port
 * @param feature feature to enable or disable
 * @param enabled TRUE to enable the selected feature or FALSE to disable it
 *
 * Note that this function is documented in the main component
 * header file, IxEthDB.h.
 *
 * @return IX_ETH_DB_SUCCESS if the operation completed
 * successfully or an appropriate error message otherwise
 */
IX_ETH_DB_PUBLIC 
IxEthDBStatus ixEthDBFeatureEnable(IxEthDBPortId portID, IxEthDBFeature feature, BOOL enable)
{
    PortInfo *portInfo;
    IxEthDBPriorityTable defaultPriorityTable;
    IxEthDBVlanSet vlanSet;
    IxEthDBStatus status = IX_ETH_DB_SUCCESS;
    BOOL portEnabled;

    IX_ETH_DB_CHECK_PORT_INITIALIZED(portID);

    portInfo    = &ixEthDBPortInfo[portID];
    portEnabled = portInfo->enabled;
    
    /* check that only one feature is selected */
    if (!ixEthDBCheckSingleBitValue(feature))
    {
        return IX_ETH_DB_FEATURE_UNAVAILABLE;
    }
            
    /* port capable of this feature? */
    if ((portInfo->featureCapability & feature) == 0)
    {
        return IX_ETH_DB_FEATURE_UNAVAILABLE;
    }
    
    /* mutual exclusion between learning and WiFi header conversion */
    if (enable && ((feature | portInfo->featureStatus) & (IX_ETH_DB_FILTERING | IX_ETH_DB_WIFI_HEADER_CONVERSION))
            == (IX_ETH_DB_FILTERING | IX_ETH_DB_WIFI_HEADER_CONVERSION))
    {
        return IX_ETH_DB_NO_PERMISSION;
    }

    /* learning must be enabled before filtering */
    if (enable && (feature == IX_ETH_DB_FILTERING) && ((portInfo->featureStatus & IX_ETH_DB_LEARNING) == 0))
    {
        return IX_ETH_DB_NO_PERMISSION;
    }
            
    /* filtering must be disabled before learning */
    if (!enable && (feature == IX_ETH_DB_LEARNING) && ((portInfo->featureStatus & IX_ETH_DB_FILTERING) != 0))
    {
        return IX_ETH_DB_NO_PERMISSION;
    }
            
    /* redundant enabling or disabling */
    if ((!enable && ((portInfo->featureStatus & feature) == 0))
        || (enable && ((portInfo->featureStatus & feature) != 0)))
    {
        /* do nothing */
        return IX_ETH_DB_SUCCESS;
    }

    /* force port enabled */
    portInfo->enabled = TRUE;

    if (enable)
    {
        /* turn on enable bit */
        portInfo->featureStatus |= feature;
        
        /* if this is VLAN/QoS set the default priority table */
        if (feature == IX_ETH_DB_VLAN_QOS)
        {
            /* turn on VLAN/QoS (most permissive mode):
                - set default 802.1Q priority mapping table, in accordance to the
                  availability of traffic classes
                - set the acceptable frame filter to accept all
                - set the Ingress tagging mode to pass-through
                - set full VLAN membership list
                - set full TTI table
                - set the default 802.1Q tag to 0 (VLAN ID 0, Pri 0, CFI 0)
                - enable TPID port extraction
            */

            portInfo->ixEthDBTrafficClassCount = portInfo->ixEthDBTrafficClassAvailable;

            /* set default 802.1Q priority mapping table - note that C indexing starts from 0, so we substract 1 here */
            ixOsalMemCopy (defaultPriorityTable, 
                (void *) ixEthIEEE802_1QUserPriorityToTrafficClassMapping[portInfo->ixEthDBTrafficClassCount - 1], 
                sizeof (defaultPriorityTable));

            /* update priority mapping and AQM queue assignments */
            status = ixEthDBPriorityMappingTableSet(portID, defaultPriorityTable);

            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBAcceptableFrameTypeSet(portID, IX_ETH_DB_ACCEPT_ALL_FRAMES);
            }

            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBIngressVlanTaggingEnabledSet(portID, IX_ETH_DB_ENABLE_VLAN);
            }

            /* set membership and TTI tables */
            ixOsalMemSet (vlanSet, 0xFF, sizeof (vlanSet));

            if (status == IX_ETH_DB_SUCCESS)
            {
                /* use the internal function to bypass PVID check */
                status = ixEthDBPortVlanTableSet(portID, portInfo->vlanMembership, vlanSet);
            }

            if (status == IX_ETH_DB_SUCCESS)
            {
                /* use the internal function to bypass PVID check */
                status = ixEthDBPortVlanTableSet(portID, portInfo->transmitTaggingInfo, vlanSet);
            }

            /* reset the PVID */
            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBPortVlanTagSet(portID, 0);
            }

            /* enable TPID port extraction */
            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBVlanPortExtractionEnable(portID, TRUE);
            }
        }
        else if (feature == IX_ETH_DB_FIREWALL)
        {
            /* firewall starts in black-list mode unless otherwise configured before *
             * note that invalid source MAC address filtering is disabled by default */
            if (portInfo->firewallMode != IX_ETH_DB_FIREWALL_BLACK_LIST
                && portInfo->firewallMode != IX_ETH_DB_FIREWALL_WHITE_LIST)
            {
                status = ixEthDBFirewallModeSet(portID, IX_ETH_DB_FIREWALL_BLACK_LIST);

                if (status == IX_ETH_DB_SUCCESS)
                {
                    status = ixEthDBFirewallInvalidAddressFilterEnable(portID, FALSE);
                }
            }
        }
        
        if (status != IX_ETH_DB_SUCCESS)
        {
            /* checks failed, disable */
            portInfo->featureStatus &= ~feature;
        }
    }
    else
    {
        /* turn off features */
        if (feature == IX_ETH_DB_FIREWALL)
        {
            /* turning off the firewall is equivalent to:
                - set to black-list mode
                - clear all the entries and download the new table
                - turn off the invalid source address checking 
            */
            
            status = ixEthDBDatabaseClear(portID, IX_ETH_DB_FIREWALL_RECORD);

            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBFirewallModeSet(portID, IX_ETH_DB_FIREWALL_BLACK_LIST);
            }

            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBFirewallInvalidAddressFilterEnable(portID, FALSE);
            }

            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBFirewallTableDownload(portID);
            }
        }
        else if (feature == IX_ETH_DB_WIFI_HEADER_CONVERSION)
        {
            /* turn off header conversion */
            status = ixEthDBDatabaseClear(portID, IX_ETH_DB_WIFI_RECORD);

            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBWiFiConversionTableDownload(portID);
            }
        }
        else if (feature == IX_ETH_DB_VLAN_QOS)
        {
            /* turn off VLAN/QoS:
                - set a priority mapping table with one traffic class
                - set the acceptable frame filter to accept all
                - set the Ingress tagging mode to pass-through
                - clear the VLAN membership list
                - clear the TTI table
                - set the default 802.1Q tag to 0 (VLAN ID 0, Pri 0, CFI 0)
                - disable TPID port extraction
            */

            /* initialize all => traffic class 0 priority mapping table */
            ixOsalMemSet (defaultPriorityTable, 0, sizeof (defaultPriorityTable));
            portInfo->ixEthDBTrafficClassCount = 1;
            status = ixEthDBPriorityMappingTableSet(portID, defaultPriorityTable);

            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBAcceptableFrameTypeSet(portID, IX_ETH_DB_ACCEPT_ALL_FRAMES);
            }

            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBIngressVlanTaggingEnabledSet(portID, IX_ETH_DB_DISABLE_VLAN);
            }

            /* clear membership and TTI tables */
            ixOsalMemSet (vlanSet, 0, sizeof (vlanSet));

            if (status == IX_ETH_DB_SUCCESS)
            {
                /* use the internal function to bypass PVID check */
                status = ixEthDBPortVlanTableSet(portID, portInfo->vlanMembership, vlanSet);
            }

            if (status == IX_ETH_DB_SUCCESS)
            {
                /* use the internal function to bypass PVID check */
                status = ixEthDBPortVlanTableSet(portID, portInfo->transmitTaggingInfo, vlanSet);
            }

            /* reset the PVID */
            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBPortVlanTagSet(portID, 0);
            }

            /* disable TPID port extraction */
            if (status == IX_ETH_DB_SUCCESS)
            {
                status = ixEthDBVlanPortExtractionEnable(portID, FALSE);
            }
        }

        if (status == IX_ETH_DB_SUCCESS)
        {
            /* checks passed, disable */
            portInfo->featureStatus &= ~feature;
        }
    }

    /* restore port enabled state */
    portInfo->enabled = portEnabled; 
    
    return status;
}

/**
 * @brief returns the status of a feature
 *
 * @param portID port ID
 * @param present location to store a boolean value indicating
 * if the feature is present (TRUE) or not (FALSE)
 * @param enabled location to store a booleam value indicating
 * if the feature is present (TRUE) or not (FALSE)
 * 
 * Note that this function is documented in the main component
 * header file, IxEthDB.h.
 *
 * @return IX_ETH_DB_SUCCESS if the operation completed
 * successfully or an appropriate error message otherwise
 */
IX_ETH_DB_PUBLIC 
IxEthDBStatus ixEthDBFeatureStatusGet(IxEthDBPortId portID, IxEthDBFeature feature, BOOL *present, BOOL *enabled)
{
    PortInfo *portInfo;
    
    IX_ETH_DB_CHECK_PORT(portID);

    IX_ETH_DB_CHECK_REFERENCE(present);

    IX_ETH_DB_CHECK_REFERENCE(enabled);
    
    portInfo = &ixEthDBPortInfo[portID];
    
    *present = (portInfo->featureCapability & feature) != 0;
    *enabled = (portInfo->featureStatus & feature) != 0;
    
    return IX_ETH_DB_SUCCESS;
}

/**
 * @brief returns the value of an EthDB property
 *
 * @param portID ID of the port
 * @param feature feature owning the property
 * @param property ID of the property
 * @param type location to store the property type into
 * @param value location to store the property value into
 *
 * Note that this function is documented in the main component
 * header file, IxEthDB.h.
 *
 * @return IX_ETH_DB_SUCCESS if the operation completed
 * successfully or an appropriate error message otherwise
 */
IX_ETH_DB_PUBLIC 
IxEthDBStatus ixEthDBFeaturePropertyGet(IxEthDBPortId portID, IxEthDBFeature feature, IxEthDBProperty property, IxEthDBPropertyType *type, void *value)
{
    IX_ETH_DB_CHECK_PORT_EXISTS(portID);

    IX_ETH_DB_CHECK_REFERENCE(type);

    IX_ETH_DB_CHECK_REFERENCE(value);
    
    if (feature == IX_ETH_DB_VLAN_QOS)
    {
        if (property == IX_ETH_DB_QOS_TRAFFIC_CLASS_COUNT_PROPERTY)
        {
            * (UINT32 *) value = ixEthDBPortInfo[portID].ixEthDBTrafficClassCount;
            *type              = IX_ETH_DB_INTEGER_PROPERTY;

            return IX_ETH_DB_SUCCESS;
        }
        else if (property >= IX_ETH_DB_QOS_TRAFFIC_CLASS_0_RX_QUEUE_PROPERTY
            && property <= IX_ETH_DB_QOS_TRAFFIC_CLASS_7_RX_QUEUE_PROPERTY)
        {
            UINT32 classDelta = property - IX_ETH_DB_QOS_TRAFFIC_CLASS_0_RX_QUEUE_PROPERTY;

            if (classDelta >= ixEthDBPortInfo[portID].ixEthDBTrafficClassCount)
            {
                return IX_ETH_DB_FAIL;
            }

            * (UINT32 *) value = ixEthDBPortInfo[portID].ixEthDBTrafficClassAQMAssignments[classDelta];
            *type              = IX_ETH_DB_INTEGER_PROPERTY;

            return IX_ETH_DB_SUCCESS;
        }
    }
    
    return IX_ETH_DB_INVALID_ARG;
}

/**
 * @brief sets the value of an EthDB property
 *
 * @param portID ID of the port
 * @param feature feature owning the property
 * @param property ID of the property
 * @param value location containing the property value
 *
 * This function implements a private property intended
 * only for EthAcc usage. Upon setting the IX_ETH_DB_QOS_QUEUE_CONFIGURATION_COMPLETE
 * property (the value is ignored), the availability of traffic classes is
 * frozen to whatever traffic class structure is currently in use. 
 * This means that if VLAN_QOS has been enabled before EthAcc
 * initialization then all the defined traffic classes will be available; 
 * otherwise only one traffic class (0) will be available.
 *
 * Note that this function is documented in the main component
 * header file, IxEthDB.h as not accepting any parameters. The
 * current implementation is only intended for the private use of EthAcc.
 *
 * Also note that once this function is called the effect is irreversible,
 * unless EthDB is complete unloaded and re-initialized.
 *
 * @return IX_ETH_DB_INVALID_ARG (no read-write properties are
 * supported in this release)
 */
IX_ETH_DB_PUBLIC 
IxEthDBStatus ixEthDBFeaturePropertySet(IxEthDBPortId portID, IxEthDBFeature feature, IxEthDBProperty property, void *value)
{
    IX_ETH_DB_CHECK_PORT_EXISTS(portID);

    if ((feature == IX_ETH_DB_VLAN_QOS) && (property == IX_ETH_DB_QOS_QUEUE_CONFIGURATION_COMPLETE))
    {
        ixEthDBPortInfo[portID].ixEthDBTrafficClassAvailable = ixEthDBPortInfo[portID].ixEthDBTrafficClassCount;

        return IX_ETH_DB_SUCCESS;
    }
    
    return IX_ETH_DB_INVALID_ARG;
}


/**
 * @brief Restore the states of EthDB Features
 *
 * @param portID ID of the port
 *
 * See IxEthDB.h for more details.
 */
IX_ETH_DB_PUBLIC 
IxEthDBStatus ixEthDBFeatureStatesRestore(IxEthDBPortId portID)
{
    PortInfo *portInfo = &ixEthDBPortInfo[portID];

    /* Check whether port if enabled */
    IX_ETH_DB_CHECK_PORT_INITIALIZED(portID);
    IX_ETH_DB_CHECK_PORT(portID);

   /* ========================  Basic ==========================
    *  Set up Port MAC Address
    */
    if (ixEthDBPortAddressSet(portID, &(portInfo->macAddr)) != IX_SUCCESS)
    {
        return IX_ETH_DB_FAIL;
    }
   
    /*
     * Set up Port Max Rx/Tx frame lengths
     */ 
    if(ixEthDBPortFrameLengthsUpdate(portID) != IX_SUCCESS)
    {
        return IX_ETH_DB_FAIL;
    }

    /* ========================  VLAN/QoS ==========================         
     * Only performs VLAN feature update if it is enabled before
     */ 
    if ((portInfo->featureStatus & IX_ETH_DB_VLAN_QOS) != 0)
    {
      /* Set VLAN Rx tag mode */
      if (ixEthDBIngressVlanModeUpdate(portID) != IX_SUCCESS)
      {
         return IX_ETH_DB_FAIL;
      }

      /* Set Default Rx VID */
      if (ixEthDBPortVlanTagSet(portID, portInfo->vlanTag) != IX_SUCCESS)
      {
         return IX_ETH_DB_FAIL;
      }
 
      /* Set PortID extraction mode */
      if (ixEthDBVlanPortExtractionEnable(portID, portInfo->portIdExtractionEnable) != IX_SUCCESS)
      {
         return IX_ETH_DB_FAIL;   
      }

      /* Set VLAN Table */
      if (ixEthDBVlanTableRangeUpdate(portID) != IX_SUCCESS)
      {
         return IX_ETH_DB_FAIL; 
      }
    } /* VLAN/QoS */

   /* ========================  Firewall ==========================         
    * Only performs Firewall feature update if it is enabled before
    */    
    if ((portInfo->featureStatus & IX_ETH_DB_FIREWALL) != 0)
    {
      if(ixEthDBFirewallTableDownload(portID) != IX_SUCCESS)
      {
	return IX_ETH_DB_FAIL;
      }  
    } /* Firewall */
  
   /* ===================== Header Conversion ==========================         
    * Only performs Header Conversion feature update if it is enabled before
    */   
    if ((portInfo->featureStatus & IX_ETH_DB_WIFI_HEADER_CONVERSION) != 0)
    {
      /* Update WiFi FC & DID */
      if (ixEthDBWiFiFrameControlDurationIDUpdate(portID) != IX_SUCCESS)
      {      
	return IX_ETH_DB_FAIL; 
      }

      /* Update BSSID */
      if (ixEthDBWiFiBSSIDSet(portID, (IxEthDBMacAddr *) portInfo->bssid) != IX_SUCCESS)
      {
	return IX_ETH_DB_FAIL;
      }

      /* Update Header Conversion Table & AP MAC Table */
      if (ixEthDBWiFiConversionTableDownload(portID) != IX_ETH_DB_SUCCESS)
      {
	return IX_ETH_DB_FAIL;
      }
    } /* Header Conversion */
  
   /* ====================== Learning & Filtering ==========================         
    * Learning & Filtering feature update is not neccessary as we can rely 
    * on EthNPE to learn the src address again. As for the entry added by client
    * earlier, it will be lost. This is the constraint as the mechanism to retrieve
    * the entry that has been added by client from NPE is not trivial.
    */

   /* ============================== STP ===================================         
    * Only performs STP feature update if it is enabled before
    */
    if ((portInfo->featureStatus & IX_ETH_DB_SPANNING_TREE_PROTOCOL) != 0)
    {    
      if (ixEthDBSpanningTreeBlockingStateSet(portID, portInfo->stpBlocked) != IX_SUCCESS)
      {
	return IX_ETH_DB_FAIL;
      }
    }

    return IX_ETH_DB_SUCCESS;
}
