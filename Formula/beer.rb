class Beer < Formula
  desc "Terminal beer physics simulator with wave dynamics and WM tracking"
  homepage "https://github.com/ewasion137/beer"
  url "https://github.com/ewasion137/beer/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "6a8c7ddc08d0f6b007e967001d6dc10797e357a9b3544d96a785efd9c0ff5319"
  license "GPL-3.0-or-later"

  def install
    system "make", "posix"
    system "make", "install", "PREFIX=#{prefix}"
  end

  test do
    assert_predicate bin/"beer", :executable?
  end
end
