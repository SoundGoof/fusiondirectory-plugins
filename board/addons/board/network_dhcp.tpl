<div id="{$sectionId}" class="plugin_section">
  <span class="legend">
    {$section}
  </span>
  <div>
    {foreach from=$attributes item=infos}
    <ul>
    {foreach from=$infos item=server}
    <li style="list-style-image:url(plugins/systems/images/select_server.png)" id="server_{$server.name}">
      {$server.link}
      <ul>
        {foreach from=$server.zones item=zone}
        <li style="list-style:disc">
          {$zone}
        </li>
        {/foreach}
      </ul>
    </li>
    {/foreach}
    </ul>
    {/foreach}
  </div>
</div>
