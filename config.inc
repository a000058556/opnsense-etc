<?php

/*
 * Copyright (C) 2015-2022 Franco Fichtner <franco@opnsense.org>
 * Copyright (C) 2009 Erik Kristensen <erik@erikkristensen.com>
 * Copyright (C) 2004-2010 Scott Ullrich <sullrich@gmail.com>
 * Copyright (C) 2003-2004 Manuel Kasper <mk@neon1.net>
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

final class product
{
    private static $instance = null;
    private static $data = null;

    public static function getInstance(): product
    {
        if (self::$instance === null) {
            self::$instance = new self();
        }

        return self::$instance;
    }

    private function __construct() // 取得核心(版本)相關資料
    {
        self::$data = json_decode(file_get_contents('/usr/local/opnsense/version/core'), true);
    }

    private function __clone()
    {
    }

    public function __wakeup()
    {
        throw new Exception('Cannot unserialize singleton');
    }

    public function __call($name, $arguments)
    {
        if (!empty(self::$data['product_' . $name])) {
            return self::$data['product_' . $name];
        }
    }
}

openlog(product::getInstance()->id(), LOG_ODELAY, LOG_USER);
register_shutdown_function('closelog');

require_once("xmlparse.inc");
require_once("legacy_bindings.inc");
require_once("certs.inc");

/*
 * Hook up the plugin system which consists of several low-profile
 * functions that can be called from within our backend code when
 * they exist.
 */
require_once('plugins.inc');

/**
 * parse config into array and return
 */
function load_config_from_file($filename)
{
    return OPNsense\Core\Config::getInstance()->toArrayFromFile($filename, listtags());
}

/****f* config/parse_config
 * NAME
 *   parse_config - Read in config.xml if needed and return $config array
 * RESULT
 *   $config      - array containing all configuration variables
 ******/
function parse_config()
{
    $cnf = OPNsense\Core\Config::getInstance();

    // return config data as array, use old "listags" construction to 
    // mark certain elements as array (even if they're not recurring)
    $config = $cnf->toArray(listtags());

    return $config;
}

/****f* config/convert_config
 * NAME
 *   convert_config - Attempt to update config.xml.
 * DESCRIPTION
 *   convert_config() reads the current global configuration
 *   and attempts to convert it to conform to the latest
 *   config.xml version. This allows major formatting changes
 *   to be made with a minimum of breakage.
 * RESULT
 *   null
 ******/
/* convert configuration, if necessary */
function convert_config($verbose = false)
{
    global $config;

    if (!isset($config['revision'])) {
        /* force a revision tag for proper handling in config history */
        write_config('Factory configuration', false);
    }

    /* chain the new migration into this function call */
    $cmd = '/usr/local/opnsense/mvc/script/run_migrations.php';
    if ($verbose) {
        passthru($cmd);
    } else {
        mwexecf($cmd);
    }

    /* register pluggable interfaces */
    $cmd = '/usr/local/sbin/pluginctl -i';
    if ($verbose) {
        passthru($cmd);
    } else {
        mwexecf($cmd);
    }

    /* factory reset on release type and plugins if necessary */
    $cmd = '/usr/local/opnsense/scripts/firmware/register.php resync_factory';
    if ($verbose) {
        passthru($cmd);
    } else {
        mwexecf($cmd);
    }

    /* reload the config as it was rewritten and saved in the script context */
    OPNsense\Core\Config::getInstance()->forceReload();
    $config = parse_config();
}

/****f* config/write_config
 * NAME
 *   write_config - Backup and write the firewall configuration.
 * DESCRIPTION
 *   write_config() handles backing up the current configuration,
 *   applying changes, and regenerating the configuration cache.
 * INPUTS
 *   $desc  - string containing the a description of configuration changes
 *   $backup  - boolean: do not back up current configuration if false.
 * RESULT
 *   null
 ******/
/* save the system configuration */
function write_config($desc = '', $backup = true)
{
    global $config;

    // Remote Address定義：(https://ithelp.ithome.com.tw/articles/10216399)
    // 最後的來源 IP 位址，它來自於 TCP 協定的來源 IP，無法以 HTTP Header 方式修改，在不被應用程式修改前提下，此值可信任。
    // 但因「最後來源 IP」，也就是說如果應用程式在 Load Balancer、Reverse Proxy 或使用 Cloudflare 之類的服務，此值僅能取得設備的 IP。
    
    // 狀況一：在 Application 伺服器上，利用 $_SERVER['REMOTE_ADDR'] 會拿到 1.2.3.4，這是我們要的
    // |---------|       |-------------|
    // | Client  |======>| Application |
    // | 1.2.3.4 |       | 11.11.11.11 |
    // |---------|       |-------------|
    
    // 狀況二：在 Application 伺服器上，利用 $_SERVER['REMOTE_ADDR'] 會拿到 22.22.22.22，這不是我們要的
    // |---------|       |---------------|        |-------------|
    // | Client  |======>| Reverse Proxy | ======>| Application |
    // | 1.2.3.4 |       | 22.22.22.22   |        | 11.11.11.11 |
    // |---------|       |---------------|        |-------------|

    // 當ip不為空值時
    if (!empty($_SERVER['REMOTE_ADDR'])) {
        // 當$_SESSION['Username']不為空值並!= 'root'時
        if (!empty($_SESSION['Username']) && ($_SESSION['Username'] != 'root')) {
            // 使用$_SESSION['Username']從$config取得使用者資料，從auth.inc調用
            $user = getUserEntry($_SESSION['Username']);
            // 當$user是array and 當權限中有"user-config-readonly"時，回傳系統錯誤
            if (is_array($user) && userHasPrivilege($user, "user-config-readonly")) {
                // okay, it's not very nice to check permissions here, but let's make it explicit while we do...
                syslog(LOG_ERR, "WARNING: User {$_SESSION['Username']} may not write config (user-config-readonly set)");
                return false;
            }
        }
    }

    // 新增或修改interfaces到config's interfaces section
    // 回傳$changed = true; or $changed = false;
    plugins_interfaces();

    $cnf = OPNsense\Core\Config::getInstance(); // 建立實例
    $cnf->fromArray($config); // 在實例中導入一個 PHP array並放入$config資料
    $revision_info = make_config_revision_entry($desc); // 取得執行修改的使用者資訊

    try {
        $cnf->save($revision_info, $backup);
    } catch (OPNsense\Core\ConfigException $e) {
        // write failure 寫入錯誤訊息
        syslog(LOG_ERR, 'WARNING: Config contents could not be saved. Could not open file!');
        return -1;
    }

    /* on successful save, serialize config back to global */
    $config = $cnf->toArray(listtags()); // 儲存成功將$cnf推回global $config

    return $config; // 回傳global $config
}

function config_restore($conffile)
{
    global $config;

    if (!file_exists($conffile)) {
        return 1;
    }

    $cnf = OPNsense\Core\Config::getInstance();
    $cnf->backup();
    $cnf->restoreBackup($conffile);

    $config = parse_config();

    write_config(sprintf('Reverted to %s', array_pop(explode('/', $conffile))), false);

    return 0;
}

function &config_read_array()
{
    // global $config; = $config =& $GLOBALS['config']
    global $config;

    $current = &$config;
    // func_get_args()函數帶入的資料 例如:刪除資料時帶入['filter', 'rule']
    foreach (func_get_args() as $key) {
        // $current[$key]若不存在 或 不是array ， $current[$key]=array();
        if (!isset($current[$key]) || !is_array($current[$key])) {
            $current[$key] = array();
        }
        $current = &$current[$key];
    }
    // 回傳取得的config_array資料
    return $current;
}

// 建立config修訂條目
function make_config_revision_entry($desc = '')
{
    // 使用者若有登入，$username = $_SESSION['Username']
    if (!empty($_SESSION['Username'])) {
        $username = $_SESSION['Username'];
    // 若沒登入，則執行cmd指令取使用者名稱
    } else {
        // shell_exec()執行CMD指令並回傳內容存於變數中(儲存成純文字內容)
        $username = '(' . trim(shell_exec('/usr/bin/whoami')) . ')';
        // /usr/bin/whoami (回覆使用者名稱) 例: root 
    }

    // 若有使用者ip位置
    if (!empty($_SERVER['REMOTE_ADDR'])) {
        $username .= '@' . $_SERVER['REMOTE_ADDR'];
    }

     // 若沒有使用者說明
    if (empty($desc)) {
        $desc = sprintf('%s made changes', $_SERVER['SCRIPT_NAME']);
    }
    // 將資料放入array
    $revision = array();
    $revision['username'] = $username;
    $revision['time'] = microtime(true); // 當前 Unix 時間戳秒數，當microtime 使用 true 參數輸出的結果具有四捨五入的功能
    $revision['description'] = $desc;

    return $revision;// 回傳使用者資訊
}

/**
 * find list of registered interfaces
 * @param array $filters list of filters to apply
 * @return array interfaces
 */

 // $filters = array(['enable' => true, 'virtual' => false])
function legacy_config_get_interfaces($filters = array())
{
    $interfaces = array();

    // 取的interfaces資料，並以$ifname => $iface(lo0 = Array(內容))方式寫入至$iface，只取$iface[0] (interfaces名稱)
    foreach (config_read_array('interfaces') as $ifname => $iface) {
        // undo stupid listags() turning our item into a new array, preventing certain names to be used as interface.
        // see src/etc/inc/xmlparse.inc
        if (isset($iface[0])) {
            // 取 Array(內容)有設值的第[0]位
            // 範例 Array ( 
            // [internal_dynamic] => 1 
            // [enable] => 1 
            // [if] => group 
            // [descr] => group 
            // [virtual] => 1 
            // [type] => group )
            $iface = $iface[0];
        }

        // apply filters
        $iface_match = true;
        foreach ($filters as $filter_key => $filter_value) {
            // array(['enable' => true, 'virtual' => false])
            // 當$iface[$filter_key]有設定時
            if ($filter_key == 'enable' && isset($iface[$filter_key])) {
                $field_value = true;
            } else {
                $field_value = isset($iface[$filter_key]) ? $iface[$filter_key] : false;
            }
            if ($field_value != $filter_value) {
                $iface_match = false;
                break;
            }
        }

        if ($iface_match && !empty($iface)) {
            $iface['descr'] = !empty($iface['descr']) ? $iface['descr'] : strtoupper($ifname);
            $iface['ipaddrv6'] = !empty($iface['ipaddrv6']) ? $iface['ipaddrv6'] : null;
            $iface['ipaddr'] = !empty($iface['ipaddr']) ? $iface['ipaddr'] : null;
            $interfaces[$ifname] = $iface;
        }
    }

    uasort($interfaces, function ($a, $b) {
        return strnatcmp($a['descr'], $b['descr']);
    });

    return $interfaces;
}

/**
 * legacy helper to generate a uuid in a similar fashion as the model code would.
 */
function generate_uuid()
{
    return sprintf(
        '%04x%04x-%04x-%04x-%04x-%04x%04x%04x',
        mt_rand(0, 0xffff),
        mt_rand(0, 0xffff),
        mt_rand(0, 0xffff),
        mt_rand(0, 0x0fff) | 0x4000,
        mt_rand(0, 0x3fff) | 0x8000,
        mt_rand(0, 0xffff),
        mt_rand(0, 0xffff),
        mt_rand(0, 0xffff)
    );
}

/**
 * parse stored json content, return empty when not found or expired
 */
function get_cached_json_content($filename, $ttl = 3600)
{
    $result = null;
    if (file_exists($filename)) {
        $fstat = stat($filename);
        if ((time() - $fstat['mtime']) < $ttl) {
            $result = json_decode(file_get_contents($filename), true);
        }
    }
    return $result;
}

$config = parse_config();

/* set timezone */
$timezone = $config['system']['timezone'];
if (!$timezone) {
    $timezone = 'Etc/UTC';
}

date_default_timezone_set($timezone);
