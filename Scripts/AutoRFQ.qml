/*

***********************************************************************************
* Copyright (C) 2019 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
import bs.terminal 1.0

RFQScript {
    property var tradeRules: [
        'EUR/XBT',
        'EUR/GBP',
        'EUR/USD'
    ]

/* This example script just resends RFQ on its expiration for selected instruments */

    onStarted: {
        log("Started");
    }
}
