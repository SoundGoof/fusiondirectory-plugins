<?php
/*
  This code is part of FusionDirectory (http://www.fusiondirectory.org/)
  Copyright (C) 2016  FusionDirectory

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

class dhcpPlugin extends simplePlugin
{
  public $mainTab = TRUE;

  protected $base;

  static function getAttributesInfo ()
  {
    return array(
      'main' => array(
        'name'  => _('View'),
        'attrs' => array(
          new HostNameAttribute (
            _('Name'), _('Name of this DHCP configuration'),
            'cn', TRUE
          ),
          new OrderedArrayAttribute (
            new StringAttribute(
              _('Options'), _('The DHCP options'),
              'dhcpOption', FALSE
            ),
            FALSE, array(), TRUE // Order disabled, Default values, Edit enabled
          ),
          new OrderedArrayAttribute (
            new StringAttribute(
              _('Statements'), _('The DHCP statements'),
              'dhcpStatements', FALSE
            ),
            FALSE, array(), TRUE
          )
        )
      )
    );
  }

  function __construct ($attribute, $dn, $attrs = NULL)
  {
    global $config;
    if (is_array($attrs)) {
      foreach ($attrs as $key => &$value) {
        if (is_array($value)) {
          $value['count'] = count($value);
        }
      }
      unset($value);
      $attrsWrapper = new stdClass();
      $attrsWrapper->attrs = $attrs;
      $this->base = dn2base($dn, get_ou('dhcpRDN'));
      parent::__construct($config, $dn, $attrsWrapper);
    } elseif ($dn == 'new') {
      $this->base = $attrs;
      parent::__construct($config, $dn);
    } else {
      trigger_error('Invalid use of dhcpPlugin class');
    }
    $this->set_acl_base($this->base);
  }

  function execute()
  {
    return parent::execute()."\n".
    '<p class="plugbottom">'."\n".
    '  <input type="submit" name="dhcp_save" value="'.msgPool::addButton().'"/>&nbsp;'."\n".
    '  <input type="submit" name="dhcp_cancel" value="'.msgPool::cancelButton().'"/>'."\n".
    '</p>';
  }

  function save()
  {
    $this->dn = $this->compute_dn();
    @DEBUG (DEBUG_TRACE, __LINE__, __FUNCTION__, __FILE__, $this->dn, 'save');
    $this->prepare_save();
    $this->attrs['cn']          = array($this->cn);
    $this->attrs['objectClass'] = $this->objectclasses;
    $this->attrs['dn']          = $this->dn;
    $this->attrs['MODIFIED']    = TRUE;
    return $this->attrs;
  }

  function compute_dn()
  {
    return 'cn='.ldap_escape_dn($this->attributesAccess['cn']->computeLdapValue()).','.get_ou('dhcpRDN').$this->base;
  }
}