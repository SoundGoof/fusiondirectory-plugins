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

class dhcpSharedNetwork extends dhcpPlugin
{
  public $objectclasses = array('dhcpSharedNetwork');

  public static function plInfo()
  {
    return array(
      'plShortName'     => _('DHCP shared network'),
      'plDescription'   => _('DHCP shared network'),
      'plCategory'      => array('dhcpConfiguration'),
      'plProvidedAcls'  => parent::generatePlProvidedAcls(static::getAttributesInfo())
    );
  }

  static function getAttributesInfo ()
  {
    $attributesInfo = parent::getAttributesInfo();
    $attributesInfo['main']['attrs'][2]->setDefaultValue(
      array(
        'deny unknown-clients',
        'deny bootp',
        'deny booting',
      )
    );
    return $attributesInfo;
  }
}
