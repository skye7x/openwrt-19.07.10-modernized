'use strict';
'require view';
'require form';
'require fs';
'require network';

return view.extend({
	load: function () {
		return Promise.all([
			network.getDevices(),
			L.resolveDefault(fs.read('/var/run/nreplatform.status'), '')
		]);
	},

	render: function (data) {
		var devices = data[0] || [];
		var status = data[1] || '';
		var m, s, o;

		m = new form.Map('nreplatform', _('NREPlatform LiteV'),
			_('Per-port network emulation: add latency, packet loss and a maximum speed to any router port or interface.'));

		s = m.section(form.GridSection, 'port', _('Port rules'));
		s.addremove = true;
		s.anonymous = true;
		s.sortable = true;

		o = s.option(form.Flag, 'enabled', _('Enabled'));
		o.default = '1';
		o.rmempty = false;
		o.editable = true;

		o = s.option(form.ListValue, 'device', _('Port'));
		o.rmempty = false;
		devices.forEach(function (d) {
			var n = d.getName();
			if (n === 'lo' || /^ifbnre/.test(n))
				return;
			o.value(n, n);
		});

		o = s.option(form.ListValue, 'direction', _('Direction'));
		o.value('egress', _('To client (download)'));
		o.value('ingress', _('From client (upload)'));
		o.value('both', _('Both'));
		o.default = 'both';

		o = s.option(form.Value, 'latency', _('Latency (ms)'));
		o.datatype = 'range(0,60000)';
		o.placeholder = '0';

		o = s.option(form.Value, 'jitter', _('Jitter (ms)'));
		o.datatype = 'range(0,60000)';
		o.placeholder = '0';
		o.modalonly = true;

		o = s.option(form.Value, 'loss', _('Packet loss (%)'));
		o.datatype = 'range(0,100)';
		o.placeholder = '0';

		o = s.option(form.Value, 'rate', _('Max speed (Mbit/s)'));
		o.datatype = 'range(0,1000000)';
		o.placeholder = '0 = unlimited';

		return m.render().then(function (mapEl) {
			return E([
				mapEl,
				E('div', { 'class': 'cbi-section' }, [
					E('h3', {}, _('Live status')),
					E('pre', {}, status || _('nreplatformd is not running.'))
				])
			]);
		});
	}
});
