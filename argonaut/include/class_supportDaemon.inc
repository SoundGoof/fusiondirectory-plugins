<?php

/*
  This code is part of FusionDirectory (http://www.fusiondirectory.org/)
  Copyright (C) 2011-2018  FusionDirectory

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*!
 * \file class_supportDaemon.inc
 * Source code for class supportDaemon
 */

require_once('jsonRPCClient.php');

/*!
 * \brief This class contains all the function needed by the argonaut
 * deployment system
 */
class supportDaemon
{
  private $s_host       = "";
  private $host_error   = "";
  private $http_options = array();
  private $ssl_options  = array();

  private $s_error      = "";
  private $b_error      = FALSE;

  private $logdir       = "";

  /* Duration in seconds while we do not try to contact server if previous task has failed */
  private $stateReloadDelay = 0; // Disabled

  /*!
   * \brief constructor
   */
  public function __construct()
  {
    global $config;
    /* This should only be the case if we call this from setup.
    __autoload()
    */
    if (!is_object($config)) {
      return;
    }
    $argonautHost = $this->get_argonaut_host();

    if ($argonautHost != "") {
      // load from config, store statically
      $this->s_host = $argonautHost;
      $this->logdir = $config->get_cfg_value("faiLogsDir");
      if ($this->logdir == "") {
        $this->logdir = FAI_LOG_DIR;
      }
    }
  }

  /*!
   * \brief Load argonaut server config from argonaut service in ldap
   */
  public function get_argonaut_host()
  {
    global $config;
    $ldap = $config->get_ldap_link();
    $ldap->cd($config->current['BASE']);
    $ldap->search(
      "(objectClass=argonautServer)",
      array(
        'cn','ipHostNumber','argonautProtocol','argonautPort',
        'argonautTimeout','argonautCertCN',
        'argonautServerToken'
      )
    );
    if ($ldap->count() == 1) {
      $ldap_infos = $ldap->fetch();
      if (isset($ldap_infos['argonautTimeout'][0])) {
        $this->http_options = array(
          'timeout' => $ldap_infos['argonautTimeout'][0]
        );
        if ($ldap_infos['argonautProtocol'][0] == 'https') {
          $this->ssl_options = array(
            'cafile'            => $config->get_cfg_value('SslCaCertPath'),
            'peer_name'         => $ldap_infos['argonautCertCN'][0],
            'verify_peer'       => TRUE,
            'verify_peer_name'  => TRUE,
          );
          $sshaMethod = new passwordMethodssha();
          $this->http_options['header'] = 'Authorization: Basic '.base64_encode('fd:'.$sshaMethod->generate_hash($ldap_infos['argonautServerToken'][0]));
        }
      }
      return $ldap_infos['argonautProtocol'][0]."://".$ldap_infos['ipHostNumber'][0].":".$ldap_infos['argonautPort'][0];
    } elseif ($ldap->count() > 1) {
      $this->host_error = _("Multiple argonaut server found. Only one is supported");
    } else {
      $this->host_error = _("There is no argonaut server configured");
    }
    return "";
  }

  /*!
   * \brief Test if the argonaut server is available
   *
   * \return boolean TRUE if the server pings, FALSE otherwise
   */
  public function is_available()
  {
    if ($this->s_host == "") {
      $this->set_error($this->host_error);
      return FALSE;
    }
    if (session::global_is_set('argonaut_state')) {
      $state = session::global_get('argonaut_state');
      if (time() - $state['date'] < $this->stateReloadDelay) {
        if ($state['error'] != "") {
          $this->set_error($state['error']);
        }
        return $state['value'];
      }
    }
    try {
      $client = new jsonRPCClient($this->s_host, $this->http_options, $this->ssl_options);
      $ok = "OK";
      $test = $client->echo($ok);
      if ($test == $ok) {
        $this->reset_error();
        return $this->new_state(TRUE);
      } else {
        $this->set_error("Received $test, expected $ok");
        return $this->new_state(FALSE);
      }
    } catch (jsonRPCClientException $e) {
      $this->set_error($e->getMessage());
      return $this->new_state(FALSE);
    }
  }

  private function saved_state ()
  {
    if (session::global_is_set('argonaut_state')) {
      $state = session::global_get('argonaut_state');
      if (time() - $state['date'] < $this->stateReloadDelay) {
        if ($state['value'] == FALSE) {
          $this->set_error($state['error']);
        }
        return $state['value'];
      } else {
        return TRUE;
      }
    }
    return TRUE;
  }

  private function new_state($state_value)
  {
    @DEBUG (DEBUG_SI, __LINE__, __FUNCTION__, __FILE__, $this->s_error, date('H.i.s').' Setting state ('.$state_value.') to ');
    session::global_set('argonaut_state', array(
      'date'  => time(),
      'error' => $this->s_error,
      'value' => $state_value
    ));
    return $state_value;
  }

  /*!
   * \brief Allows simply appending a new argonautAction
   *
   * \param String $event The event to append.
   *
   * \return TRUE if correctly appended FALSE otherwise
   */
  public function append($event)
  {
    if (!($event instanceof argonautAction)) {
      $this->set_error('Event object passed to append was not of the expected argonautAction type');
      return $this->new_state(FALSE);
    }

    try {
      /* Append an entry */
      return $this->append_call($event->action, array_values($event->targets), $event->computeData());
    } catch (FusionDirectoryException $e) {
      $this->set_error($e->getMessage());
      return $this->new_state(FALSE);
    }
  }

  /*!
   * \brief Allows simply append a call to a function
   *
   * \param string $action    The action to call
   *
   * \param Array  $targets   The concerned mac addresses. If this is not an array the result will be unpacked.
   *
   * \param Array  $data      The data to send (timestamp, periodic and args, see Argonaut protocol)
   *
   * \return the returned value if correctly executed and FALSE otherwise
   */
  public function append_call($action, $targets, $data)
  {
    @DEBUG (DEBUG_SI, __LINE__, __FUNCTION__, __FILE__, array('action' => $action, 'targets' => $targets, 'data' => $data), date('H.i.s').' Appending call to');
    if ($this->s_host == '') {
      $this->set_error($this->host_error);
      return FALSE;
    }
    if ($this->saved_state() == FALSE) {
      return FALSE;
    }

    /* Append an entry */
    try {
      $client = new jsonRPCClient($this->s_host, $this->http_options, $this->ssl_options);
      if (!is_array($targets)) {
        $tmpTargets = array($targets);
      } else {
        $tmpTargets = $targets;
      }
      $status = $client->action($action, $tmpTargets, $data);
      @DEBUG (DEBUG_SI, __LINE__, __FUNCTION__, __FILE__, $status, date('H.i.s').' Answer');
      $this->reset_error();
      $this->new_state(TRUE);
      if (is_array($status) && !is_array($targets) && isset($status[0])) {
        /* Return first value */
        return $status[0];
      } else {
        return $status;
      }
    } catch (jsonRPCClientRequestErrorException $e) {
      $this->set_error($e->getMessage());
      // connection is OK
      $this->new_state(TRUE);
      return FALSE;
    } catch (jsonRPCClientNetworkErrorException $e) {
      $this->set_error($e->getMessage());
      return $this->new_state(FALSE);
    }
  }

  /*!
   * \brief  Sets an error message, which can be returned with get_error().
   *
   * \param  String $str The Error message,
   */
  private function set_error($str)
  {
    @DEBUG (DEBUG_SI, __LINE__, __FUNCTION__, __FILE__, $str, date('H.i.s').' Setting error to');
    $this->b_error = TRUE;
    $this->s_error = $str;
  }


  /*!
   * \brief  Resets the error message.
   */
  private function reset_error()
  {
    $this->b_error = FALSE;
    $this->s_error = "";
  }


  /*!
   * \brief  Checks if an error occured.
   *
   * \return boolean returns TRUE or FALSE, whether there is an error or not.
   */
  public function is_error()
  {
    return $this->b_error;
  }


  /*!
   * \brief  Returns the last error.
   *
   * \return string Returns the last error.
   */
  public function get_error()
  {
    $str = $this->s_error;
    $ret = "";
    if (is_string($str)) {
      $ret = $str;
    } else {
      foreach ($str as $msg) {
        $ret .= $msg."\n";
      }
    }
    $ret = str_replace(" ", "&nbsp;", $ret);
    return nl2br($ret);
  }

  /*!
   * \brief format queue entries
   *
   * \param array $entries The entries to format
   *
   * \return array The entries formatted
   */
  private function format_entries($entries)
  {
    global $config;
    timezone::get_default_timezone();
    $entries_r  = array();
    $macInfos   = array();
    $ldap       = $config->get_ldap_link();
    $ldap->cd($config->current['BASE']);
    foreach ($entries as &$entry) {
      $entry_r['TIMESTAMP']   = date('YmdHis', $entry['data']['timestamp']);
      $entry_r['MACADDRESS']  = $entry['target'];

      /* Do an ldap search only if we haven't done it for the same mac before */
      if (!isset($macInfos[$entry_r['MACADDRESS']])) {
        $types = array('workstation','server','terminal');
        foreach ($types as $type) {
          $objects = objects::ls($type, array('dn' => 'raw', 'cn' => 1), NULL, '(macAddress='.$entry_r['MACADDRESS'].')');
          if (count($objects) > 1) {
            trigger_error('Duplicated mac address '.$entry_r['MACADDRESS'].' in LDAP');
          }
          if (count($objects) > 0) {
            $macInfos[$entry_r['MACADDRESS']] = array(
              'dn'    => reset($objects)['dn'],
              'type'  => $type,
              'cn'    => reset($objects)['cn']
            );
            break;
          }
        }
      }
      if (isset($macInfos[$entry_r['MACADDRESS']])) {
        $entry_r['PLAINNAME']   = $macInfos[$entry_r['MACADDRESS']]['cn'];
        $entry_r['TARGETDN']    = $macInfos[$entry_r['MACADDRESS']]['dn'];
        $entry_r['TARGETTYPE']  = $macInfos[$entry_r['MACADDRESS']]['type'];
      } else {
        $entry_r['PLAINNAME']   = $entry_r['MACADDRESS'];
      }

      $entry_r['ERROR']     = $entry['error'];
      $entry_r['PROGRESS']  = $entry['progress'];

      $entry_r['PERIODIC']  = (isset($entry['data']['periodic']) ? $entry['data']['periodic'] : NULL);
      $entry_r['STATUS']    = $entry['status'];
      $entry_r['SUBSTATUS'] = $entry['substatus'];
      $entry_r['ID']        = $entry['id'];
      $entry_r['HEADERTAG'] = $entry['action'];

      $entries_r[] = $entry_r;
    }
    unset($entry);
    return $entries_r;
  }


  /*!
   * \brief Returns an array containing all queued entries.
   *
   * \return array All queued entries as an array or FALSE if there is an error.
   */
  public function get_queued_entries()
  {
    if ($this->s_host == "") {
      $this->set_error($this->host_error);
      return FALSE;
    }
    if ($this->saved_state() == FALSE) {
      return FALSE;
    }
    timezone::get_default_timezone();
    try {
      $client = new jsonRPCClient($this->s_host, $this->http_options, $this->ssl_options);
      $entries = $client->get_entries();
    } catch (jsonRPCClientException $e) {
      $this->set_error($e->getMessage());
      return $this->new_state(FALSE);
    }
    $entries = $this->format_entries($entries);
    $this->reset_error();
    $this->new_state(TRUE);
    return $entries;
  }

  /*!
   * \brief  Returns an entry containing all requested ids.
   *
   * \param array $ids The IDs of the entries we want to return.
   *
   * \return array  Array of the requested entries.
  */
  public function get_entries_by_id($ids)
  {
    return $this->get_entries("get_entries_by_id", $ids);
  }

  /*!
   * \brief  Removes a set of entries from the argonaut queue.
   *
   * \param array $ids The IDs to remove.
   *
   * \return boolean TRUE on success FALSE otherwise.
   */
  public function remove_entries($ids)
  {
    if (!is_array($ids)) {
      trigger_error("Requires an array as parameter.");
      return;
    }
    if ($this->s_host == "") {
      $this->set_error($this->host_error);
      return FALSE;
    }
    if ($this->saved_state() == FALSE) {
      return FALSE;
    }

    try {
      $client = new jsonRPCClient($this->s_host, $this->http_options, $this->ssl_options);
      $client->remove_entries($ids);
      $this->reset_error();
      return $this->new_state(TRUE);
    } catch (jsonRPCClientRequestErrorException $e) {
      $this->set_error($e->getMessage());
      // connection is OK
      $this->new_state(TRUE);
      return FALSE;
    } catch (jsonRPCClientNetworkErrorException $e) {
      $this->set_error($e->getMessage());
      return $this->new_state(FALSE);
    }
  }

  /*!
   * \brief Removes an entry from the argonaut queue.
   *
   * \param integer $id The ID of the entry we want to remove.
   *
   * \return Boolean TRUE on success.
  */
  public function remove_entry($id)
  {
    return $this->remove_entries(array($id));
  }

  /*!
   * \brief Returns an entry from the argonaut queue
   *
   * \param integer $id The ID of the entry we want to return.
   *
   * \return array  array of the requested entry.
  */
  public function get_entry_by_id($id)
  {
    return $this->get_entries_by_id(array($id));
  }

  /*!
   * \brief  Ask argonaut to process a set of entries now
   *
   * \param  array $ids  The ids of the entries to process immediatly
   *
   * \return boolean Returns TRUE on success FALSE otherwise.
  */
  public function process_entries_now($ids)
  {
    if (!is_array($ids)) {
      trigger_error("Requires an array as first parameter.");
      return;
    }
    if ($this->s_host == "") {
      $this->set_error($this->host_error);
      return FALSE;
    }
    if ($this->saved_state() == FALSE) {
      return FALSE;
    }

    try {
      $client = new jsonRPCClient($this->s_host, $this->http_options, $this->ssl_options);
      $client->process_entries_now($ids);
      $this->reset_error();
      return $this->new_state(TRUE);
    } catch (jsonRPCClientRequestErrorException $e) {
      $this->set_error($e->getMessage());
      // connection is OK
      $this->new_state(TRUE);
      return FALSE;
    } catch (jsonRPCClientNetworkErrorException $e) {
      $this->set_error($e->getMessage());
      return $this->new_state(FALSE);
    }
  }

  /*!
   * \brief Check if a workstation is currently installing
   *
   * \param  string $mac The mac address of the workstation
   *
   * \return boolean Returns TRUE if currently installing FALSE otherwise.
   */
  public function is_currently_installing($mac)
  {
    $evts = $this->get_entries_by_mac(array($mac));
    if ($evts) {
      foreach ($evts as $evt) {
        if (($evt['STATUS'] == "processing") &&
            in_array($evt['HEADERTAG'], array("reinstall","update"))) {

          return TRUE;
        }
      }
    }
    return FALSE;
  }

  /*!
   * \brief  Returns an array containing all entries concerning the macs passed.
   *
   * \param array $macs  The mac addresses of the workstations for which we want entries.
   *
   * \return array  The entries for the requested mac addresses.
   */
  public function get_entries_by_mac($macs)
  {
    return $this->get_entries("get_entries_by_mac", $macs);
  }

  /*!
   * \brief  Get entries from argonaut server with given function and format entries before to return them
   *
   * \param function $func  The JSONRPC method to use for retrieving addresses
   *
   * \param array $address  The mac addresses or ids of the entries we want.
   *
   * \return Array   The entries for the requested addresses.
   */
  private function get_entries($func, $address)
  {
    if (!is_array($address)) {
      trigger_error("Requires an array as parameter.");
      return;
    }
    if ($this->s_host == "") {
      $this->set_error($this->host_error);
      return FALSE;
    }
    if ($this->saved_state() == FALSE) {
      return FALSE;
    }

    try {
      $client = new jsonRPCClient($this->s_host, $this->http_options, $this->ssl_options);
      $entries = $client->$func($address);
      $entries = $this->format_entries($entries);
      $this->reset_error();
      $this->new_state(TRUE);
      return $entries;
    } catch (jsonRPCClientRequestErrorException $e) {
      $this->set_error($e->getMessage());
      // connection is OK
      $this->new_state(TRUE);
      return FALSE;
    } catch (jsonRPCClientNetworkErrorException $e) {
      $this->set_error($e->getMessage());
      return $this->new_state(FALSE);
    }
  }

  /*!
   * \brief  Returns log file information for a given mac address
   *
   * \param  string $mac The mac address to fetch logs for.
   *
   * \return Array A Multidimensional array containing log infos.
   *
   * \code
   *  MAC_00_01_6C_9D_B9_FA['install_20080311_090900']['FILES'][0]=debconf.log
   *  MAC_00_01_6C_9D_B9_FA['install_20080311_090900']['FILES'][1]=syslog.log
   * \endcode
   */
  public function get_log_info_for_mac($mac)
  {
    if (empty($mac)) {
      return FALSE;
    }
    /* read mac directory */
    $logfiledir = $this->logdir.strtolower("/$mac/");
    if (!is_dir($logfiledir)) {
      return FALSE;
    }
    $dates = scandir($logfiledir);
    if (!$dates) {
      /* directory does not exists */
      return FALSE;
    }
    $ret = array();
    foreach ($dates as $date) {
      if ($date == "." || $date == "..") {
          continue;
      }
      $ret[$date]['DATE_STR']  = $date;
      $ret[$date]['REAL_DATE'] = strtotime(preg_replace("/[^0-9]*/", "", $date));
      foreach (scandir("$logfiledir/$date") as $file) {
        if ($file == "." || $file == "..") {
          continue;
        }
        $ret[$date]['FILES'][] = $file;
      }
    }
    return $ret;
  }

  /*!
   * \brief  Get a precise log file on a certain date for a mac address
   *
   * \param string $mac The mac address to fetch logs for.
   *
   * \param string $date The date for the log to fetch.
   *
   * \param string $file The log filename.
   *
   */
  public function get_log_file($mac, $date, $file)
  {
    $path = $this->logdir.strtolower("/$mac")."/$date/$file";
    if (is_file($path)) {
      $this->reset_error();
      $lines = file_get_contents($path);
      if ($lines === FALSE) {
        $this->set_error(sprintf(_('Could not read "%s"!'), $path));
      }
      return $lines;
    } else {
      $this->set_error(sprintf(_('"%s" is not a file!'), $path));
      return FALSE;
    }
  }

  /*!
   * \brief  Returns true if an argonaut daemon is running on the pc
   *
   * \param string $mac The mac address to ping
   *
   * \return boolean TRUE if the workstation is running an argonaut daemon
  */
  public function ping($mac)
  {
    if ($this->s_host == "") {
      $this->set_error($this->host_error);
      return FALSE;
    }
    if ($this->saved_state() == FALSE) {
      return FALSE;
    }
    try {
      $client = new jsonRPCClient($this->s_host, $this->http_options, $this->ssl_options);
      $this->reset_error();
      $this->new_state(TRUE);
      return $client->ping($mac);
    } catch (jsonRPCClientRequestErrorException $e) {
      $this->set_error($e->getMessage());
      // connection is OK
      $this->new_state(TRUE);
      return FALSE;
    } catch (jsonRPCClientNetworkErrorException $e) {
      $this->set_error($e->getMessage());
      return $this->new_state(FALSE);
    }
  }

  /*!
   * \brief  Get packages list for the given release
   *
   * \param string  $release The release we want the packages of. If empty all releases are used.
   *
   * \param array   $attrs   The attributes we want for each packages.
   * (all attributes from Packages file can put, plus version, distribution, has_template and templace)
   *
   * \param array   $filters The filters we want to use. An or is performed between array cases.
   *
   * \param integer $from    The indice of the package we want to start from
   *
   * \param integer $to    The indice of the package we want to stop to.
   *
   * \param boolean $package_list    True if filters is an array of package names that needs exact matching, false if it is an array of perl regexp
   *
   * \return Array   List of packages that fit the given parameters.
  */
  public function FAI_get_packages($release, $attrs, $filters, $from = -1, $to = -1, $package_list = FALSE)
  {
    if ($this->s_host == "") {
      $this->set_error($this->host_error);
      return array();
    }
    if ($this->saved_state() == FALSE) {
      return array();
    }

    // filters is a package name list
    if ($package_list) {
      foreach ($filters as &$filter) {
        $filter = "^".$this->escape_perl_chars($filter)."$";
      }
      unset($filter);
    }
    try {
      $client = new jsonRPCClient($this->s_host, $this->http_options, $this->ssl_options);
      $res = $client->get_packages($release, $attrs, $filters, $from, $to);
      $to_return = array();
      $attrs = array_map('strtoupper', $attrs);
      foreach ($res as $distribution => $packages) {
        $words = preg_split("/\//", $distribution);
        $distribution = $words[0];
        $section = $words[1];

        if (in_array("DISTRIBUTION", $attrs)) {
          foreach ($packages as &$package) {
            $package['DISTRIBUTION'] = $distribution;
          }
          unset($package);
        }
        if (in_array("SECTION", $attrs)) {
          foreach ($packages as &$package) {
            // FIXME : do we want to do this? It will replace "contrib/misc by contrib, for instance
            $package['SECTION'] = $section;
          }
          unset($package);
        }
        $to_return = array_merge($to_return, $packages);
      }
      $this->reset_error();
      $this->new_state(TRUE);
      return $to_return;
    } catch (jsonRPCClientRequestErrorException $e) {
      $this->set_error($e->getMessage());
      // connection is OK
      $this->new_state(TRUE);
      return array();
    } catch (jsonRPCClientNetworkErrorException $e) {
      $this->set_error($e->getMessage());
      $this->new_state(FALSE);
      return array();
    }
  }

  /*!
   * \brief Escape perl regexp special chars
   *
   * \param string $str The string that needs escaping
   *
   * \return string Escaped string
  */
  public function escape_perl_chars($str)
  {
    $patterns = array("/\+/", "/\*/", "/\./");
    $escaped  = array("\+",   "\*",   "\."  );
    return preg_replace($patterns, $escaped, $str);
  }

  /*! \brief  Get available kernels packages
   *
   * \param string $release The release we want kernels of
   *
   * \return array  All kernel packages for the given release
  */
  public function FAI_get_kernels($release)
  {
    $kernels = $this->FAI_get_packages($release, array("package"), array("linux-image", '^kernel$'));
    foreach ($kernels as &$kernel) {
      $kernel = $kernel['PACKAGE'];
    }
    unset($kernel);
    return $kernels;
  }

  /*!
   * \brief Clean queue
   *
   * \param array $mac The macs that have to be removed from the queue
   *
   * \return TRUE if success or FALSE otherwise
  */
  public function clean_queue_from_mac($macs)
  {
    if ($this->s_host == "") {
      $this->set_error($this->host_error);
      return FALSE;
    }
    if ($this->saved_state() == FALSE) {
      return FALSE;
    }

    if (empty($macs)) {
      return TRUE;
    }
    if (!is_array($macs)) {
      $macs = array($macs);
    }

    try {
      $client = new jsonRPCClient($this->s_host, $this->http_options, $this->ssl_options);
      $this->reset_error();
      $taskids = $client->get_entries_by_mac($macs);
      $client->remove_entries($taskids);
      return $this->new_state(TRUE);
    } catch (jsonRPCClientRequestErrorException $e) {
      $this->set_error($e->getMessage());
      // connection is OK
      $this->new_state(TRUE);
      return FALSE;
    } catch (jsonRPCClientNetworkErrorException $e) {
      $this->set_error($e->getMessage());
      return $this->new_state(FALSE);
    }
  }
}

?>
