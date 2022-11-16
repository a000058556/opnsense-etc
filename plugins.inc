<?php

/*
 * Copyright (C) 2016-2022 Franco Fichtner <franco@opnsense.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * scan plugins for legacy system
 * @return array
 */
function plugins_scan()
{
    $path = '/usr/local/etc/inc/plugins.inc.d/';
    $clash = '/usr/local/etc/inc/';
    $ext = '.inc';

    $ret = array();

    // glob()取得匹配之檔案路徑存為array
    // 取/usr/local/etc/inc/plugins.inc.d/中的所有.int結尾的檔案路徑
    // $plugins = Array ( 
    // [0] => /usr/local/etc/inc/plugins.inc.d/core.inc 
    // [1] => /usr/local/etc/inc/plugins.inc.d/dhcpd.inc 
    // [2] => /usr/local/etc/inc/plugins.inc.d/dnsmasq.inc 
    // [3] => /usr/local/etc/inc/plugins.inc.d/dpinger.inc 
    // ...
    // [18] => /usr/local/etc/inc/plugins.inc.d/webgui.inc 
    // )
    $plugins = glob($path . '*' . $ext);
    if (!is_array($plugins)) {
        return $ret;
    }

    sort($plugins); // 覆蓋KEY值，由內容小到大(A~z)重新排序。

    foreach ($plugins as $plugin) {
        // preg_replace( pattern, replacement, subject [, int limit ] )  用於正則表達式的搜索和替換。
        // pattern	    正則表達式 => /\/usr\/local\/etc\/inc\/plugins\.inc\.d\//
        // replacement	替換的內容 => ''
        // subject	    需要匹配替換的對象 => $plugin
        // limit	    可選，指定替換的個數，如果省略limit 或者其值為-1，則所有的匹配項都會被替換

        // preg_quote()需要參數str 並向其中每個正則表達式語法中的字符前增加一個反斜線'\'取消符號功能。
        // 斜線在正則表達中並非特殊功能符合，因此帶入'/', 讓其可被添加反斜線
        // preg_quote($path, '/') = \/usr\/local\/etc\/inc\/plugins\.inc\.d\/
        $name = preg_replace('/' . preg_quote($path, '/') . '/', '', $plugin); // 去路徑得到core.inc 
        // preg_quote($ext, '/') = \.inc
        $name = preg_replace('/' . preg_quote($ext, '/') . '/', '', $name); // 去副檔名.inc得到core
        // file_exists()如果指定的文件或目錄存在則返回 true。 若clash內有檔案相對應。
        if (file_exists($clash . $name . '.inc') || file_exists($clash . $name . '.class')) {
            /*
             * Congratulations, you found the reason why your plugin doesn't
             * work!  It seems that you're using a name that is already taken
             * by the base system.  Please change the name of your plugin.
             *
             * A traceable call stack requires unique prefixes, which is what
             * will prevent this from working.  Do not remove this check
             * without discussing the consequences with the authors.
             */
            continue; // 回到迴圈的開頭，執行「下一次」的迴圈
        }
        $ret[$name] = $plugin; 
        // $name = 去副檔名.inc ， $ret資料形式示意: array( [core] => '/usr/local/etc/inc/plugins.inc.d/core.inc' )
    }

    return $ret; // 回傳clash內有相對應檔案的plugin array (plugin名稱 => 路徑+檔名)
}

function plugins_services()
{
    $services = array();

    foreach (plugins_scan() as $name => $path) {
        try {
            include_once $path;
        } catch (\Error $e) {
            error_log($e);
        }
        $func = sprintf('%s_services', $name);
        if (function_exists($func)) {
            foreach ($func() as $work) {
                $services[] = $work;
            }
        }
    }

    uasort($services, function ($a, $b) {
        return strcasecmp($a['name'], $b['name']);
    });

    return $services;
}

function plugins_devices()
{
    $devices = array();

    foreach (plugins_scan() as $name => $path) {
        try {
            include_once $path;
        } catch (\Error $e) {
            error_log($e);
        }
        $func = sprintf('%s_devices', $name);
        if (function_exists($func)) {
            foreach ($func() as $work) {
                $devices[] = $work;
            }
        }
    }

    return $devices;
}

function plugins_cron()
{
    $jobs = array();

    foreach (plugins_scan() as $name => $path) {
        try {
            include_once $path;
        } catch (\Error $e) {
            error_log($e);
        }
        $func = sprintf('%s_cron', $name);
        if (function_exists($func)) {
            foreach ($func() as $work) {
                $jobs[] = $work;
            }
        }
    }

    return $jobs;
}

/**
 * list of known applications for syslog-ng to use
 * Only to be used to register syslog-ng custom destinations (facility is program in syslog-ng's context)
 */
function plugins_syslog()
{
    $syslogs = array();

    foreach (plugins_scan() as $name => $path) {
        try {
            include_once $path;
        } catch (\Error $e) {
            error_log($e);
        }
        $func = sprintf('%s_syslog', $name);
        if (function_exists($func)) {
            foreach ($func() as $plugin_syslog => $plugin_details) {
                $syslogs[$plugin_syslog] = $plugin_details;
            }
        }
    }

    return $syslogs;
}

/**
 * Register new or changed interfaces into config's interfaces section.
 * Every <plugin>_interface should return a named array containing the interface unique identifier and properties.
 *
 */
function plugins_interfaces()
{
    global $config;

    $stale_interfaces = []; // 舊的interfaces資料
    $changed = false;

    // mark previous dynamic registrations stale
    if (isset($config['interfaces'])) {
        // 取$config['interfaces']放至 key($intf_ref) value($intf_data)
        foreach ($config['interfaces'] as $intf_ref => $intf_data) {
            // isset($intf_data[0]['internal_dynamic'])的判斷是為了group
            if (isset($intf_data[0]['internal_dynamic']) || isset($intf_data['internal_dynamic'])) {
                $stale_interfaces[$intf_ref] = 1;
                // $stale_interfaces內容示意: Array ( [lo0] => 1 [group]=> 1 [test_for_group]=> 1 )
            }
        }
    }

    // register / update interfaces

    // 透過plugins_scan()載入有clash檔案的plugin名稱 => 檔案位置
    foreach (plugins_scan() as $name => $path) { 
        try {
            include_once $path; // 透過檔案位置引入舊檔案?
        } catch (\Error $e) {
            error_log($e);
        }
        $func = sprintf('%s_interfaces', $name); // core_interfaces
        // function_exists()檢查是否存在此function，存在返回TRUE，否則返回FALSE
        if (function_exists($func)) {
            // plugins有function的檔名:
            // 'ipsec_interfaces'
            // 'loopback_interfaces'
            // 'openvpn_interfaces'
            // 'pf_interfaces'
            foreach ($func() as $intf_ref => $intf_data) { // 取得修改後檔案?
            // 取loopback.inc中的loopback_interfaces()作為範例
            // function loopback_interfaces()
            // {
            //     return [
            //         'lo0' => [
            //             'descr' => gettext('Loopback'),
            //             'enable' => true,
            //             'if' => 'lo0',
            //             'ipaddr' => '127.0.0.1',
            //             'ipaddrv6' => '::1',
            //             'subnet' => '8',
            //             'subnetv6' => '128',
            //             'type' => 'none',
            //             'virtual' => true,
            //         ]
            //     ];
            // }

            // $intf_ref =>'lo0'
            // $intf_data => array['descr' => gettext('Loopback'),...]
                if (is_array($intf_data)) {
                    // mark interface used
                    if (isset($stale_interfaces[$intf_ref])) {
                        unset($stale_interfaces[$intf_ref]); // 清除key與內容
                    }
                    if (isset($config['interfaces'][$intf_ref][0])) {
                        // undo stupid listags() turning our item into a new array, see src/etc/inc/xmlparse.inc
                        $intf_config = &config_read_array('interfaces', $intf_ref, 0);
                    } else {
                        $intf_config = &config_read_array('interfaces', $intf_ref); // 載入config資料
                    }
                    // 若internal_dynamic未設置
                    if (!isset($intf_config['internal_dynamic'])) {
                        $intf_config['internal_dynamic'] = true;
                        $changed = true;
                    }
                    // traverse and diff interface properties with known configuration
                    foreach ($intf_data as $prop_name => $prop_value) {
                        // 若$intf_config[$prop_name]為空值 and $prop_value不為空(新增) or $intf_config[$prop_name] != $prop_value(修改)
                        if ((empty($intf_config[$prop_name]) && !empty($prop_value)) || $intf_config[$prop_name] != $prop_value) {
                            $intf_config[$prop_name] = $prop_value; // 寫入變更後資料
                            $changed = true;
                        }
                    }
                }
            }
        }
    }

    // cleanup registrations 清理註冊
    foreach ($stale_interfaces as $intf_ref => $no_data) {
        if (isset($config['interfaces'][$intf_ref])) {
            unset($config['interfaces'][$intf_ref]);
            $changed = true;
        }
    }

    return $changed;
}

function plugins_firewall($fw)
{
    foreach (plugins_scan() as $name => $path) {
        try {
            include_once $path;
        } catch (\Error $e) {
            error_log($e);
        }
        $func = sprintf('%s_firewall', $name);
        if (function_exists($func)) {
            $func($fw);
        }
    }

    return $fw;
}

function plugins_configure($hook, $verbose = false, $args = array())
{
    array_unshift($args, $verbose);

    syslog(LOG_NOTICE, sprintf('plugins_configure %s (%s)', $hook, implode(',', $args)));

    foreach (plugins_scan() as $name => $path) {
        try {
            include_once $path;
        } catch (\Error $e) {
            error_log($e);
        }
        $func = sprintf('%s_configure', $name);
        if (function_exists($func)) {
            foreach ($func() as $when => $worker) {
                if ($hook == $when && is_array($worker)) {
                    foreach ($worker as $task) {
                        /*
                         * An optional argument count parameter can be
                         * given by the plugin, which allows to securely
                         * pull more info from the configure call spot.
                         */
                        if (substr_count($task, ':')) {
                            list($argf, $argc) = explode(':', $task);
                        } else {
                            $argf = $task;
                            $argc = null;
                        }
                        if (empty($argc) || !is_numeric($argc)) {
                            $argc = 1;
                        }
                        if ($argc > count($args)) {
                            $argc = count($args);
                        }
                        syslog(LOG_NOTICE, sprintf(
                            'plugins_configure %s (execute task : %s(%s))',
                            $hook,
                            $argf,
                            implode(',', array_slice($args, 0, $argc))
                        ));
                        try {
                            call_user_func_array($argf, array_slice($args, 0, $argc));
                        } catch (\Error $e) {
                            error_log($e);
                        }
                    }
                }
            }
        }
    }
}

function plugins_run($hook, $verbose = false, $args = array())
{
    array_unshift($args, $verbose);
    $ret = [];

    syslog(LOG_DEBUG, sprintf('plugins_run %s (%s)', $hook, implode(',', $args)));

    foreach (plugins_scan() as $name => $path) {
        try {
            include_once $path;
        } catch (\Error $e) {
            error_log($e);
        }
        $func = sprintf('%s_run', $name);
        if (function_exists($func)) {
            foreach ($func() as $when => $task) {
                if ($hook == $when) {
                    /*
                     * An optional argument count parameter can be
                     * given by the plugin, which allows to securely
                     * pull more info from the configure call spot.
                     */
                    if (substr_count($task, ':')) {
                        list($argf, $argc) = explode(':', $task);
                    } else {
                        $argf = $task;
                        $argc = null;
                    }
                    if (empty($argc) || !is_numeric($argc)) {
                        $argc = 1;
                    }
                    if ($argc > count($args)) {
                        $argc = count($args);
                    }
                    syslog(LOG_DEBUG, sprintf(
                        'plugins_run %s (execute task : %s(%s))',
                        $hook,
                        $argf,
                        implode(',', array_slice($args, 0, $argc))
                    ));
                    try {
                        $ret[$name] = call_user_func_array($argf, array_slice($args, 0, $argc));
                    } catch (\Error $e) {
                        error_log($e);
                    }
                }
            }
        }
    }

    return $ret;
}

function plugins_xmlrpc_sync()
{
    $sync_settings = array();
    foreach (plugins_scan() as $name => $path) {
        try {
            include_once $path;
        } catch (\Error $e) {
            error_log($e);
        }
        $func = sprintf('%s_xmlrpc_sync', $name);
        if (function_exists($func)) {
            foreach ($func() as $helper) {
                if (!empty($helper['id']) && !empty($helper['section'])) {
                    $sync_settings[$helper['id']] = $helper;
                    if (empty($helper['help'])) {
                        $sync_settings[$helper['id']]['help'] = sprintf(gettext('Synchronize the %s configuration to the other HA host.'), $helper['description']);
                    }
                    if (empty($helper['services'])) {
                        $sync_settings[$helper['id']]['services'] = [];
                    } elseif (!is_array($helper['services'])) {
                        $sync_settings[$helper['id']]['services'] = [$helper['services']];
                    }
                    unset($sync_settings[$helper['id']]['id']);
                }
            }
        }
    }
    return $sync_settings;
}
