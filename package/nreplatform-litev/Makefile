include $(TOPDIR)/rules.mk

PKG_NAME:=nreplatform-litev
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_MAINTAINER:=NREPlatform
PKG_LICENSE:=Apache-2.0

include $(INCLUDE_DIR)/package.mk

define Package/nreplatform-litev
  SECTION:=net
  CATEGORY:=Network
  TITLE:=NREPlatform LiteV - per-port latency, packet loss and speed limits
  DEPENDS:=+libstdcpp +libuci +kmod-sched +kmod-sched-core +kmod-ifb +luci-base
endef

define Package/nreplatform-litev/description
  C++ daemon (nreplatformd) that applies latency, jitter, packet loss and a
  maximum speed to individual router ports using netem over rtnetlink,
  configured from /etc/config/nreplatform, plus a LuCI page under
  Network -> NREPlatform LiteV.
endef

define Package/nreplatform-litev/conffiles
/etc/config/nreplatform
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		$(TARGET_CONFIGURE_OPTS) \
		CXXFLAGS="$(TARGET_CXXFLAGS)" \
		CPPFLAGS="$(TARGET_CPPFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/nreplatform-litev/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/nreplatformd $(1)/usr/sbin/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/nreplatform.init $(1)/etc/init.d/nreplatform
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/nreplatform.config $(1)/etc/config/nreplatform
	$(INSTALL_DIR) $(1)/etc/uci-defaults
	$(INSTALL_BIN) ./files/90_nreplatform $(1)/etc/uci-defaults/90_nreplatform
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) ./files/menu.json $(1)/usr/share/luci/menu.d/nreplatform-litev.json
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./files/acl.json $(1)/usr/share/rpcd/acl.d/nreplatform-litev.json
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/nreplatform
	$(INSTALL_DATA) ./files/ports.js $(1)/www/luci-static/resources/view/nreplatform/ports.js
endef

$(eval $(call BuildPackage,nreplatform-litev))
